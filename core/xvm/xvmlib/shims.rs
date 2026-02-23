use std::fs;
use std::path::{Path, PathBuf};
use std::process::Command;
use std::sync::OnceLock;

use indexmap::IndexMap;

use crate::versiondb::VData;

fn expand_xlings_vars(value: &str) -> String {
    let home = std::env::var("XLINGS_HOME").unwrap_or_default();
    let data = std::env::var("XLINGS_DATA").unwrap_or_default();
    let subos = std::env::var("XLINGS_SUBOS").unwrap_or_default();
    value
        .replace("${XLINGS_HOME}", &home)
        .replace("${XLINGS_DATA}", &data)
        .replace("${XLINGS_SUBOS}", &subos)
}

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
    bindings: IndexMap<String, String>,
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
            bindings: IndexMap::new(),
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

    pub fn add_binding(&mut self, target: &str, version: &str) {
        if self.bindings.contains_key(target) {
            eprintln!("Warning: bind for [ {} ] already exists, overwriting", target);
        }
        self.bindings.insert(target.to_string(), version.to_string());
    }

    pub fn get_bindings(&self) -> &IndexMap<String, String> {
        &self.bindings
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
            bindings: if self.bindings.is_empty() {
                None
            } else {
                Some(self.bindings.clone())
            },
        }
    }

    pub fn set_vdata(&mut self, vdata: &VData) {
        self.set_path(&expand_xlings_vars(&vdata.path));
        if let Some(envs) = &vdata.envs {
            let expanded: Vec<(String, String)> = envs
                .iter()
                .map(|(k, v)| (k.clone(), expand_xlings_vars(v)))
                .collect();
            self.add_envs(
                expanded.iter().map(|(k, v)| (k.as_str(), v.as_str())).collect::<Vec<_>>().as_slice()
            );
        }
        if let Some(bindings) = &vdata.bindings {
            self.bindings = bindings.clone();
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

        // If path is set and we run the program by name (no alias), resolve executable (path/name or path/bin/name)
        let program_path: Option<std::path::PathBuf> = if self.alias.is_none() && !self.path.is_empty() {
            // Resolve relative path against workspace dir (XLINGS_SUBOS) for package bootstrap entries
            let mut base: std::path::PathBuf = if Path::new(&self.path).is_relative() {
                XVM_WORKSPACE_DIR.get().map(|w| Path::new(w).join(&self.path)).unwrap_or_else(|| PathBuf::from(&self.path))
            } else {
                PathBuf::from(&self.path)
            };
            // Normalize base (resolve ".." and ".") so exists() and Command::new() find the binary reliably
            if let Ok(canon) = fs::canonicalize(&base) {
                base = canon;
            }
            let candidates: Vec<std::path::PathBuf> = if cfg!(target_os = "windows") {
                let names = [
                    format!("{}.exe", self.name),
                    format!("{}.bat", self.name),
                ];
                let mut out = Vec::new();
                for n in &names {
                    out.push(base.join(n));
                }
                for n in &names {
                    out.push(base.join("bin").join(n));
                }
                out
            } else {
                vec![
                    base.join(&self.name),
                    base.join("bin").join(&self.name),
                ]
            };
            let exe_path = candidates.into_iter().find(|p| p.exists());
            match exe_path {
                Some(p) => Some(p),
                None => {
                    eprintln!("xvm: executable not found at {}", base.join(&self.name).display());
                    eprintln!("  (also tried {})", base.join("bin").join(&self.name).display());
                    #[cfg(target_os = "windows")]
                    eprintln!("  (looked for {}.exe and {}.bat in path and path/bin)", self.name);
                    eprintln!("  path: {}", self.path);
                    eprintln!("  hint: install with e.g. xlings install {}@<version>", self.name);
                    return 1;
                }
            }
        } else {
            None
        };

        let path_env = self.get_path_env();
        let path_env_value = if let Some(ref p) = program_path {
            if let Some(parent) = p.parent() {
                let parent_str = parent.to_string_lossy();
                if parent_str != self.path.as_str() {
                    let sep = if cfg!(target_os = "windows") { ";" } else { ":" };
                    format!("{}{}{}", parent_str, sep, path_env.1)
                } else {
                    path_env.1
                }
            } else {
                path_env.1
            }
        } else {
            path_env.1
        };

        let mut cmd = if let Some(ref path) = program_path {
            Command::new(path)
        } else {
            Command::new(&target)
        };
        let status = cmd
            .args(&alias_args)
            .args(&self.args)
            .env("PATH", &path_env_value)
            .envs([self.get_ld_library_path_env()])
            .envs(build_extended_envs(self.envs.clone()))
            .status();

        match status {
            Ok(status) => {
                status.code().unwrap_or(1)
            },
            Err(e) => {
                #[cfg(target_os = "linux")]
                if e.kind() == std::io::ErrorKind::NotFound
                    && program_path.is_some()
                {
                    let bin_path = program_path.as_ref().unwrap();
                    let ld_env = self.get_ld_library_path_env();
                    let extended = build_extended_envs(self.envs.clone());
                    let status_fallback = run_via_system_loader(
                        bin_path,
                        &alias_args,
                        &self.args,
                        &path_env_value,
                        &ld_env,
                        &extended,
                    );
                    if let Ok(s) = status_fallback {
                        return s.code().unwrap_or(1);
                    }
                }
                eprintln!("Failed to execute `xvm run {}`: {}", target, e);
                if e.kind() == std::io::ErrorKind::NotFound {
                    if let Some(ref path) = program_path {
                        eprintln!("  path: {}", path.display());
                        eprintln!("  hint: the binary or a required library/interpreter may be missing (e.g. in minimal/CI environments).");
                    }
                }
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

        // print bindings
        if !self.bindings.is_empty() {
            let mut bindings_string = String::from("Bindings: ");
            for (target, version) in &self.bindings {
                bindings_string.push_str(&format!("{}@{} ", target, version));
            }
            println!("{}", bindings_string.trim_end());
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
        if !fs::metadata(dir).is_ok() {
            fs::create_dir_all(dir).unwrap();
        }

        let src = XVM_SHIM_BIN.get().unwrap();
        if fs::hard_link(src, &target_shim).is_err() {
            fs::copy(src, &target_shim).unwrap();
        }
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

#[cfg(target_os = "linux")]
fn system_loader_paths() -> Vec<&'static std::path::Path> {
    use std::path::Path;
    [
        Path::new("/lib64/ld-linux-x86-64.so.2"),
        Path::new("/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"),
    ]
    .to_vec()
}

#[cfg(target_os = "linux")]
fn run_via_system_loader(
    bin_path: &Path,
    alias_args: &[&str],
    args: &[String],
    path_env_value: &str,
    ld_env: &(String, String),
    extended_envs: &[(String, String)],
) -> std::io::Result<std::process::ExitStatus> {
    let loader = system_loader_paths()
        .into_iter()
        .find(|p| p.exists())
        .ok_or_else(|| std::io::Error::new(std::io::ErrorKind::NotFound, "no system ELF loader found"))?;
    let mut cmd = Command::new(loader);
    cmd.arg(bin_path)
        .args(alias_args)
        .args(args)
        .env("PATH", path_env_value);
    if ld_env.0 != "XVM_ENV_NULL" {
        cmd.env(&ld_env.0, &ld_env.1);
    }
    for (k, v) in extended_envs {
        cmd.env(k, v);
    }
    cmd.status()
}