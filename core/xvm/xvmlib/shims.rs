use std::fs;
use std::process::Command;
use std::sync::OnceLock;

use crate::versiondb::VData;

pub static XVM_ALIAS_WRAPPER: &str = "xvm-alias";
pub static XVM_SHIM_BIN: OnceLock<String> = OnceLock::new();

// TODO: shim-mode, direct-mode
pub enum Type {
    Direct,
    Shim,
    Alias,
}

pub struct Program {
    name: String,
    version: String,
    alias: Option<String>,
    path: String,
    envs: Vec<(String, String)>,
    args: Vec<String>,
}

impl Program {
    pub fn new(name: &str, version: &str) -> Self {
        Self {
            name: name.to_string(),
            version: version.to_string(),
            alias: None,
            path: String::new(),
            envs: Vec::new(),
            args: Vec::new(),
        }
    }

    pub fn name(&self) -> &str {
        &self.name
    }

    pub fn version(&self) -> &str {
        &self.version
    }

    pub fn set_alias(&mut self, alias: &str) {
        self.alias = Some(alias.to_string());
    }

    pub fn add_env(&mut self, key: &str, value: &str) {
        self.envs.push((key.to_string(), value.to_string()));
    }

    pub fn add_envs(&mut self, envs: &[(&str, &str)]) {
        for (key, value) in envs {
            self.envs.push((key.to_string(), value.to_string()));
        }
    }

    pub fn set_path(&mut self, path: &str) {
        self.path = path.to_string();
    }

    pub fn add_arg(&mut self, arg: &str) {
        self.args.push(arg.to_string());
    }

    pub fn add_args(&mut self, args: &Vec<String>) {
        for arg in args {
            self.args.push(arg.clone());
        }
    }

    pub fn vdata(&self) -> VData {
        VData {
            alias: self.alias.clone(),
            path: self.path.clone(),
            envs: if self.envs.is_empty() {
                None
            } else {
                Some(self.envs.iter().cloned().collect())
            },
        }
    }

    pub fn set_vdata(&mut self, vdata: &VData) {
        self.set_path(&vdata.path);
        if let Some(envs) = &vdata.envs {
            self.add_envs(envs.iter().map(|(k, v)| (k.as_str(), v.as_str())).collect::<Vec<_>>().as_slice());
        }
        self.alias = vdata.alias.clone();
    }

    pub fn run(&self) -> i32 {
        //println!("Running Program [{}], version {:?}", self.name, self.version);
        //println!("Args: {:?}", self.args);
        //println!("Envs: {:?}", self.envs);
        //println!("Extended Envs: {:?}", build_extended_envs(self.envs.clone()));

        let mut target = self.name.clone();
        let mut alias_args: Vec<&str> = Vec::new();

        if let Some(alias) = &self.alias {
            target = XVM_ALIAS_WRAPPER.to_string();
            #[cfg(target_os = "windows")]
            {
                target.push_str(".bat");
            }
            alias_args = alias.split_whitespace().collect();
        }

        let status = Command::new(&target)
            .args(alias_args)
            .args(&self.args)
            .env("PATH", self.get_path_env())
            .envs(build_extended_envs(self.envs.clone()))
            .status();

        match status {
            Ok(status) => {
                status.code().unwrap_or(1)
            },
            Err(e) => {
                eprintln!("Failed to execute `xvm run {}`: {}", target, e);
                1
            }
        }

        //println!("Program [{}] finished", self.name);
    }

    // TODO: 
    pub fn link_to(&self, dir: &str, recover: bool) {
        let libname = self.alias.clone().unwrap();
        let lib_real_path = format!("{}/{}", self.path, libname);
        let lib_path = format!("{}/{}", dir, libname);

        // try create dir
        if !fs::metadata(dir).is_ok() {
            fs::create_dir_all(dir).unwrap();
        }
        
        // check if the symlink already exists, remove it
        if fs::symlink_metadata(&lib_path).is_ok() && recover {
            fs::remove_file(&lib_path).unwrap();
        } else {
            // if symlink does not exist and recover is false, do nothing
            return;
        }

        // try to create a symlink
        #[cfg(unix)]
        {
            if let Err(e) = std::os::unix::fs::symlink(&lib_real_path, &lib_path) {
                eprintln!("Failed to create symlink for {}: {}", libname, e);
            }
        }
        #[cfg(windows)]
        {
            if let Err(e) = std::os::windows::fs::symlink_file(&lib_real_path, &lib_path) {
                eprintln!("Failed to create symlink for {}: {}", libname, e);
            }
        }
    }

    pub fn save_to(&self, dir: &str) {
        // TODO: optimize - shim-mode, direct-mode
        try_create(&self.name, dir);
    }

    ///// private methods

    fn get_path_env(&self) -> String {
        let current_path = std::env::var("PATH").unwrap_or_default();

        let new_path = if current_path.is_empty() {
            self.path.to_string()
        } else {
            // if self.path is empty, then use current_path
            if self.path.is_empty() {
                current_path
            } else {
                let separator = if cfg!(target_os = "windows") { ";" } else { ":" };
                format!("{}{}{}", self.path, separator, current_path)
            }
        };

        new_path
    }
}

pub fn init(bindir: &str) {
    XVM_SHIM_BIN.get_or_init(|| {
        let bin = shim_file("xvm-shim", bindir);
        bin
    });

    create_shim_script_file(XVM_ALIAS_WRAPPER, bindir, "");
}

pub fn try_create(target: &str, dir: &str) {
    let target_shim = shim_file(target, dir);
    if !fs::metadata(&target_shim).is_ok() {
        // check dir
        if !fs::metadata(dir).is_ok() {
            fs::create_dir_all(dir).unwrap();
        }

        // cp bindir/xvm-shim to target_shim
        fs::copy(XVM_SHIM_BIN.get().unwrap(), &target_shim).unwrap();
    }
}

pub fn delete(target: &str, dir: &str) {
    let target_shim = shim_file(target, dir);
    if fs::metadata(&target_shim).is_ok() {
        fs::remove_file(&target_shim).unwrap();
    }
}

fn create_shim_script_file(target: &str, dir: &str, content: &str) {

    let (sfile, args_placeholder, header) = shim_script_file(target, dir);

    if !fs::metadata(&sfile).is_ok() {

        if !fs::metadata(dir).is_ok() {
            fs::create_dir_all(dir).unwrap();
        }

        //println!("creating shim file for [{}]", target);

        fs::write(&sfile, &format!("{}\n{} {}", header, content, args_placeholder)).unwrap();

        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&sfile, PermissionsExt::from_mode(0o755)).unwrap();
        }
    }
}

fn shim_file(target: &str, dir: &str) -> String {
    // if is windows, then use .exe
    if cfg!(target_os = "windows") {
        format!("{}/{}.exe", dir, target)
    } else {
        format!("{}/{}", dir, target)
    }
}

fn shim_script_file<'a>(target: &str, dir: &'a str) -> (String, &'a str, &'a str) {
    let sfile: String;
    let args_placeholder: &str;
    let header: &str;
    if cfg!(target_os = "windows") {
        header = "@echo off";
        args_placeholder = "%*";
        sfile = format!("{}/{}.bat", dir, target);
    } else {
        header = "#!/bin/sh";
        args_placeholder = "\"$@\"";
        sfile = format!("{}/{}", dir, target);
    }

    (sfile, args_placeholder, header)
}

fn build_extended_envs(envs: Vec<(String, String)>) -> Vec<(String, String)> {
    let sep = if cfg!(windows) { ";" } else { ":" };

    envs.into_iter()
        .map(|(key, val)| {
            let new_val = match std::env::var(&key) {
                Ok(existing_val) => format!("{}{}{}", val, sep, existing_val),
                Err(_) => val,
            };
            (key, new_val)
        })
        .collect()
}