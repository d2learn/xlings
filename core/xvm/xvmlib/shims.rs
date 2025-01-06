use std::fs;
use std::process::Command;

use crate::versiondb::VData;

pub static XVM_ALIAS_WRAPPER: &str = "xvm-alias";

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

    pub fn run(&self) {
        //println!("Running Program [{}], version {:?}", self.name, self.version);
        //println!("Args: {:?}", self.args);
        //println!("Envs: {:?}", self.envs);

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

        Command::new(&target)
            .args(alias_args)
            .args(&self.args)
            .env("PATH", self.get_path_env())
            .envs(self.envs.iter().cloned())
            .status()
            .expect("failed to execute process");

        //println!("Program [{}] finished", self.name);
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
            let separator = if cfg!(target_os = "windows") { ";" } else { ":" };
            format!("{}{}{}", self.path, separator, current_path)
        };

        new_path
    }
}

pub fn try_create(target: &str, dir: &str) {
    create_shim_file(
        target, dir,
        &format!("xvm run {} --args", target)
    );
}

pub fn delete(target: &str, dir: &str) {
    let (filename, _, _) = shim_file(target, dir);

    if fs::metadata(&filename).is_ok() {
        fs::remove_file(&filename).unwrap();
    }
}

pub fn create_shim_file(target: &str, dir: &str, content: &str) {

    let (sfile, args_placeholder, header) = shim_file(target, dir);

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

fn shim_file<'a>(target: &str, dir: &'a str) -> (String, &'a str, &'a str) {
    let sfile: String;
    let args_placeholder: &str;
    let header: &str;
    if cfg!(target_os = "windows") {
        header = "@echo off";
        args_placeholder = "%*";
        sfile = format!("{}/{}.bat", dir, target);
    } else {
        header = "#!/bin/sh";
        args_placeholder = "$@";
        sfile = format!("{}/{}", dir, target);
    }

    (sfile, args_placeholder, header)
}