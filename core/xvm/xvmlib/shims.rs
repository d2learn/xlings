use std::fs;
use std::process::Command;

use crate::versiondb::VData;

// TODO
pub enum Type {
    Symlink, // SymlinkWrapper,
    XvmRun, // Auto Detect Version
}

pub struct Program {
    name: String,
    version: String,
    filename: Option<String>,
    path: String,
    envs: Vec<(String, String)>,
    args: Vec<String>,
}

impl Program {
    pub fn new(name: &str, version: &str) -> Self {
        Self {
            name: name.to_string(),
            version: version.to_string(),
            filename: None,
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

    pub fn set_filename(&mut self, filename: &str) {
        self.filename = Some(filename.to_string());
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
            filename: self.filename.clone(),
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
        self.filename = vdata.filename.clone();
    }

    pub fn run(&self) {
        println!("Running Program [{}], version {:?}", self.name, self.version);
        println!("Args: {:?}", self.args);
        //println!("Envs: {:?}", self.envs);

        // if filename isnot empty, then use it, otherwise use name
        let target_program = self.filename.as_ref().unwrap_or(&self.name);

        Command::new(&target_program)
            .args(&self.args)
            .env("PATH", self.get_path_env())
            .envs(self.envs.iter().cloned())
            .status()
            .expect("failed to execute process");

        println!("Program [{}] finished", self.name);
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

pub fn create(target: &str, dir: &str) {
    println!("Saving Program to {}", dir);

    if !fs::metadata(dir).is_ok() {
        fs::create_dir_all(dir).unwrap();
    }

    // create shim-script-file for windows and unix
    let (filename, args_placeholder) = shim_file(target, dir);

    if !fs::metadata(&filename).is_ok() {
        fs::write(&filename, &format!("xvm run {} {}",
            target, args_placeholder
        )).unwrap();

        #[cfg(unix)]
        {
            use std::os::unix::fs::PermissionsExt;
            fs::set_permissions(&filename, PermissionsExt::from_mode(0o755)).unwrap();
        }
    }
}

pub fn delete(target: &str, dir: &str) {
    let (filename, _) = shim_file(target, dir);

    if fs::metadata(&filename).is_ok() {
        fs::remove_file(&filename).unwrap();
    }
}

fn shim_file<'a>(target: &str, dir: &'a str) -> (String, &'a str) {
    let filename: String;
    let args_placeholder: &str;
    if cfg!(target_os = "windows") {
        args_placeholder = "%*";
        filename = format!("{}/{}.bat", dir, target);
    } else {
        args_placeholder = "$@";
        filename = format!("{}/{}", dir, target);
    }

    (filename, args_placeholder)
}