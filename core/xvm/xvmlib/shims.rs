use std::fs;
use std::process::Command;

use crate::config::VData;

// TODO
pub enum Type {
    Symlink, // SymlinkWrapper,
    XvmRun, // Auto Detect Version
}

pub struct Program {
    name: String,
    version: String,
    envs: Vec<(String, String)>,
    args: Vec<String>,
}

impl Program {
    pub fn new(name: &str, version: &str) -> Self {
        Self {
            name: name.to_string(),
            version: version.to_string(),
            envs: Vec::new(),
            args: Vec::new(),
        }
    }

    pub fn save_to(&self, dir: &str) {
        println!("Saving Program to {}", dir);

        if !fs::metadata(dir).is_ok() {
            fs::create_dir_all(dir).unwrap();
        }

        // create shim-script-file for windows and unix
        let filename: String;
        let args_placeholder: &str;
        if cfg!(target_os = "windows") {
            args_placeholder = "%*";
            filename = format!("{}/{}.bat", dir, self.name);
        } else {
            args_placeholder = "$@";
            filename = format!("{}/{}", dir, self.name);
        }

        fs::write(&filename, &format!("echo run {} {}",
            self.name, args_placeholder
        )).unwrap();

        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&filename, PermissionsExt::from_mode(0o755)).unwrap();
        }
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
        let current_path = std::env::var("PATH").unwrap_or_default();
        let new_path = if current_path.is_empty() {
            path.to_string()
        } else {
            let separator = if cfg!(target_os = "windows") { ";" } else { ":" };
            format!("{}{}{}", path, separator, current_path)
        };

        self.add_env("PATH", &new_path);
    }

    pub fn add_arg(&mut self, arg: &str) {
        self.args.push(arg.to_string());
    }

    pub fn add_args(&mut self, args: &[&str]) {
        for arg in args {
            self.args.push(arg.to_string());
        }
    }

    pub fn set_vdata(&mut self, version_data: &VData) {
        self.set_path(&version_data.path);
        if let Some(envs) = &version_data.envs {
            self.add_envs(envs.iter().map(|(k, v)| (k.as_str(), v.as_str())).collect::<Vec<_>>().as_slice());
        }
        if let Some(name) = &version_data.name {
            self.name = name.clone();
        }
    }

    pub fn run(&self) {
        println!("Running Program [{}], version {:?}", self.name, self.version);
        println!("Args: {:?}", self.args);
        //println!("Envs: {:?}", self.envs);
        Command::new(&self.name)
            .args(&self.args)
            .envs(self.envs.iter().cloned())
            .status()
            .expect("failed to execute process");
        println!("Program [{}] finished", self.name);
    }
}