use std::sync::OnceLock;
use std::path::PathBuf;
use std::env;

static RUNDIR: OnceLock<PathBuf> = OnceLock::new();

#[allow(dead_code)]
pub fn rundir() {
    RUNDIR.get_or_init(|| {
        // get current runtime directory
        env::current_dir().expect("Failed to get current directory")
    });
}

pub fn versiondb_file() -> String {
    format!("{}/versions.xvm.yaml", platform::xvm_homedir())
}

pub fn workspace_file() -> String {
    format!("{}/workspace.xvm.yaml", platform::xvm_homedir())
}

#[allow(dead_code)]
pub fn bindir() -> String {
    platform::bindir()
}

#[allow(dead_code)]
pub fn shimdir() -> String {
    format!("{}/shims", platform::xvm_homedir())
}

#[allow(dead_code)]
pub fn print_baseinfo() {
    println!("XVM Home: {}", platform::xvm_homedir());
    println!("XVM Bindir: {}", bindir());
    println!("XVM VersionDB: {}", versiondb_file());
    println!("XVM Workspace: {}", workspace_file());
}

pub mod platform {
    //use std::env;
    use super::*;

    static HOMEDIR: OnceLock<String> = OnceLock::new();

    pub fn homedir() -> String {
        HOMEDIR.get_or_init(|| {
            #[cfg(target_os = "windows")]
            {
                env::var("USERPROFILE").expect("Failed to get USERPROFILE environment variable")
            }

            #[cfg(not(target_os = "windows"))]
            {
                env::var("HOME").expect("Failed to get HOME environment variable")
            }
        }).clone()
    }

    pub fn bindir() -> String {
        if cfg!(target_os = "windows") {
            "C:/users/public/.xlings_data/bin".to_string()
        } else {
            format!("{}/.xlings_data/bin", homedir())
        }
    }

    pub fn xvm_homedir() -> String {
        if cfg!(target_os = "windows") {
            "C:/users/public/.xlings_data/xvm".to_string()
        } else {
            format!("{}/.xlings_data/xvm", homedir())
        }
    }
}