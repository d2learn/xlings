use std::fs;
use std::process::Command;
use std::sync::OnceLock;

use crate::versiondb::VData;

pub static XVM_ALIAS_WRAPPER: &str = "xvm-alias";
/*
TODO: WORKPACE ? .xlings/xvm-workspace
workspace-dir
--bin
--lib
*/
pub static XVM_WORKSPACE_DIR: OnceLock<String> = OnceLock::new();
pub static XVM_WORKSPACE_BINDIR: OnceLock<String> = OnceLock::new();
pub static XVM_WORKSPACE_LIBDIR: OnceLock<String> = OnceLock::new();

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
    vtype: Option<String>,
    filename: Option<String>, // lib: target file
    alias: Option<String>, // source file
    path: String,
    path_env: String,
    ld_library_path_env: Option<String>,
    envs: Vec<(String, String)>,
    args: Vec<String>,
}

impl Program {
    pub fn new(name: &str, version: &str) -> Self {
        Self {
            name: name.to_string(),
            version: version.to_string(),
            vtype: None,
            filename: None,
            alias: None,
            path: String::new(),
            path_env: String::new(),
            ld_library_path_env: None,
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

    pub fn set_type(&mut self, vtype: &str) {
        self.vtype = Some(vtype.to_string());
    }

    pub fn set_filename(&mut self, filename: &str) {
        self.filename = Some(filename.to_string());
    }

    pub fn set_alias(&mut self, alias: &str) {
        self.alias = Some(alias.to_string());
    }

    pub fn add_env(&mut self, key: &str, value: &str) {
        // if key is PATH or LD_LIBRARY_PATH/DYLD_LIBRARY_PATH, then append to the existing value
        // PATH -> self.path_env
        // LD_LIBRARY_PATH -> self.ld_library_path_env
        let mut spe = if cfg!(target_os = "windows") { ";" } else { ":" };
        if key == "PATH" {
            if self.path_env.is_empty() { spe = ""; }
            self.path_env = format!("{}{}{}", self.path_env, spe, value);
        } else if key == "LD_LIBRARY_PATH" || key == "DYLD_LIBRARY_PATH" {
            if self.ld_library_path_env.is_none() { spe = ""; }
            self.ld_library_path_env = Some(format!("{}{}{}",
                self.ld_library_path_env.clone().unwrap_or_default(),
                spe, value
            ));
        } else {
            self.envs.push((key.to_string(), value.to_string()));
        }
    }

    pub fn add_envs(&mut self, envs: &[(&str, &str)]) {
        for (key, value) in envs {
            self.add_env(key, value);
        }
    }

    pub fn set_path(&mut self, path: &str) {
        self.path = path.to_string();
        self.add_env("PATH", path);
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
        let mut envs_tmp = self.envs.clone();
        // push path_env and ld_library_path_env to envs
        if !self.path_env.is_empty() {
            envs_tmp.push(("PATH".to_string(), self.path_env.clone()));
        }
        if let Some(ld_path) = &self.ld_library_path_env {
            envs_tmp.push((
                if cfg!(target_os = "linux") { "LD_LIBRARY_PATH" } else { "DYLD_LIBRARY_PATH" }.to_string(),
                ld_path.clone())
            );
        }
        VData {
            alias: self.alias.clone(),
            path: self.path.clone(),
            envs: if envs_tmp.is_empty() {
                None
            } else {
                Some(envs_tmp.iter().cloned().collect())
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
            .envs([self.get_path_env()]) // .env("PATH", self.get_path_env())
            // .env("XXLD_LIBRARY_PATH", self.get_ld_library_path_env())
            .envs([self.get_ld_library_path_env()])
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
        let source_libname = self.alias.clone().unwrap();
        let target_libname = self.filename.clone().unwrap_or_else(|| source_libname.clone());
        let lib_real_path = format!("{}/{}", self.path, source_libname);
        let lib_path = format!("{}/{}", dir, target_libname);

        //println!("link_to: {} -> {}", lib_real_path, lib_path);

        // try create dir
        if !fs::metadata(dir).is_ok() {
            fs::create_dir_all(dir).unwrap();
        }

        // check if the symlink already exists, remove it
        if fs::symlink_metadata(&lib_path).is_ok() {
            if recover {
                fs::remove_file(&lib_path).unwrap();
            } else {
                // if symlink does not exist and recover is false, do nothing
                println!("link_to: symlink {} already exists, skipping", lib_path);
                return;
            }
        }

        // try to create a symlink
        #[cfg(unix)]
        {
            //println!("link_to: creating symlink for {} -> {}", source_libname, lib_real_path);
            if let Err(e) = std::os::unix::fs::symlink(&lib_real_path, &lib_path) {
                eprintln!("Failed to create symlink for {}: {}", source_libname, e);
            }
        }
        #[cfg(windows)]
        {
            if let Err(e) = std::os::windows::fs::symlink_file(&lib_real_path, &lib_path) {
                // TODO: handle windows symlink / hardlink / copy?
                eprintln!("link_to: no implementation for windows system");
                eprintln!("Failed to create symlink for {}: {}", source_libname, e);
            }
        }
    }

    ///// private methods

    fn get_path_env(&self) -> (String, String) {

        let separator = if cfg!(target_os = "windows") { ";" } else { ":" };
        let mut new_path = self.path_env.clone();

        // append workspace/bin to path
        // self.path_env priority is higher than workspace/bin
        // workspace/bin priority is higher than current(system) PATH
        new_path.push_str(separator);
        new_path.push_str(XVM_WORKSPACE_BINDIR.get().unwrap());

        if cfg!(target_os = "windows") { // libpath -> path
            // on windows, we need to append the workspace/lib to path
            new_path.push_str(separator);
            new_path.push_str(XVM_WORKSPACE_LIBDIR.get().unwrap());
        }

        let current_path = std::env::var("PATH").unwrap_or_default();

        new_path = if current_path.is_empty() {
            self.path_env.to_string()
        } else {
            // if self.path_env is empty, then use current_path
            if self.path_env.is_empty() {
                current_path
            } else {
                format!("{}{}{}", self.path_env, separator, current_path)
            }
        };

        ("PATH".to_string(), new_path)
    }

    pub fn get_ld_library_path_env(&self) -> (String, String) {
        if let Some(mut ld_path) = self.ld_library_path_env.clone() {

            ld_path.push(':');
            ld_path.push_str(XVM_WORKSPACE_LIBDIR.get().unwrap());

            // linux and macos(DYLD_LIBRARY_PATH)
            let ld_library_path_env_name = if cfg!(target_os = "linux") {
                "LD_LIBRARY_PATH"
            } else if cfg!(target_os = "macos") {
                "DYLD_LIBRARY_PATH"
            } else { // unsupported OS - WINDOWS? 
                return ("XVM_ENV_NULL".to_string(), String::new());
            };

            let current_ld_path = std::env::var(ld_library_path_env_name).unwrap_or_default();

            let new_ld_path = if current_ld_path.is_empty() {
                ld_path.to_string()
            } else {
                //let separator = if cfg!(target_os = "windows") { ";" } else { ":" };
                format!("{}:{}", ld_path, current_ld_path)
            };

            (ld_library_path_env_name.to_string(), new_ld_path)
        } else {
            // return a null env
            (String::from("XVM_ENV_NULL"), String::new())
        }
    }

    pub fn print_info(&self) {
        println!("\t[xvm-shim-0.0.5]");

        println!("Program: {}", self.name);
        println!("Version: {}", self.version);

        let mut target_dir = XVM_WORKSPACE_BINDIR.get().unwrap().clone();

        if let Some(vtype) = &self.vtype {
            println!("Type: {}", vtype);
            if vtype == "lib" {
                target_dir = XVM_WORKSPACE_LIBDIR.get().unwrap().clone();
            }
        }

        // SPath: source path
        let source_filename = self.alias.clone().unwrap_or_else(|| {
            if cfg!(target_os = "windows") {
                format!("{}.exe", self.name)
            } else {
                self.name.clone()
            }
        });
        if self.path.is_empty() {
            if self.alias.is_some() {
                println!("Alias: {}", self.alias.as_ref().unwrap());
            }
        } else {
            let source_path = format!("{}/{}", self.path, source_filename);
            println!("SPath: {}", source_path);
        };

        // TPath: target path
        let target_filename = self.filename.clone().unwrap_or_else(|| {
            if cfg!(target_os = "windows") {
                format!("{}.exe", self.name)
            } else {
                self.name.clone()
            }
        });
        let target_path = format!("{}/{}", target_dir, target_filename);
        println!("TPath: {}", target_path);

        if !self.envs.is_empty() {
            println!("Envs:");
            if !self.path_env.is_empty() {
                println!("  PATH={}", self.path_env);
            }
            if let Some(ld_path) = &self.ld_library_path_env {
                println!("  LD_LIBRARY_PATH={}", ld_path);
            }
            for (key, value) in &self.envs {
                println!("  {}={}", key, value);
            }
        }
        if !self.args.is_empty() {
            println!("Args: {:?}", self.args);
        }
    }
}

pub fn init(workspace_dir: &str) {
    XVM_WORKSPACE_DIR.get_or_init(|| {
        workspace_dir.to_string()
    });

    let bindir = format!("{}/bin", workspace_dir);

    XVM_WORKSPACE_BINDIR.get_or_init(|| {
        bindir.clone()
    });

    XVM_WORKSPACE_LIBDIR.get_or_init(|| {
        format!("{}/lib", workspace_dir)
    });

    XVM_SHIM_BIN.get_or_init(|| {
        let bin = shim_file("xvm-shim", &bindir);
        bin
    });

    create_shim_script_file(XVM_ALIAS_WRAPPER, &bindir, "");
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