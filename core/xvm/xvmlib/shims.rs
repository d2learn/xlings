use std::fs;
use std::process::Command;

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

    pub fn add_arg(&mut self, arg: &str) {
        self.args.push(arg.to_string());
    }

    pub fn add_args(&mut self, args: &[&str]) {
        for arg in args {
            self.args.push(arg.to_string());
        }
    }

    pub fn run(&self) {
        println!("Running Program [{}], version {:?}", self.name, self.version);
        println!("Args: {:?}", self.args);
        Command::new(&self.name)
            .args(&self.args)
            .envs(self.envs.iter().cloned())
            .status()
            .expect("failed to execute process");
        println!("Program [{}] finished", self.name);
    }
}