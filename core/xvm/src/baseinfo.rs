use std::sync::OnceLock;
use std::path::PathBuf;
use std::env;

static RUNDIR: OnceLock<PathBuf> = OnceLock::new();

pub static WORKSPACE_FILE : &str = ".workspace.xvm.yaml";

#[allow(dead_code)]
pub fn rundir() {
    RUNDIR.get_or_init(|| {
        // get current runtime directory
        env::current_dir().expect("Failed to get current directory")
    });
}

pub fn versiondb_file() -> String {
    format!("{}/versions.xvm.yaml", platform::xvm_datadir())
}

pub fn workspace_file() -> String {
    format!("{}/{}", platform::xvm_datadir(), WORKSPACE_FILE)
}

#[allow(dead_code)]
pub fn bindir() -> String {
    format!("{}/bin", platform::xvm_homedir())
}

#[allow(dead_code)]
pub fn libdir() -> String {
    format!("{}/lib", platform::xvm_homedir())
}

#[allow(dead_code)]
pub fn shimdir() -> String {
    format!("{}/shims", platform::xvm_datadir())
}

#[allow(dead_code)]
pub fn print_baseinfo() {
    println!("XVM Home: {}", platform::xvm_homedir());
    println!("XVM Data: {}", platform::xvm_datadir());
    println!("XVM Bindir: {}", bindir());
    println!("XVM VersionDB: {}", versiondb_file());
    println!("XVM Workspace: {}", workspace_file());
}

pub mod platform {
    //use std::env;
/*
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
*/
    pub fn xvm_homedir() -> String {
        if cfg!(target_os = "windows") {
            r#"C:\Users\Public\xlings\.xlings_data"#.to_string()
        } else if cfg!(target_os = "macos") {
            "/Users/xlings/.xlings_data".to_string()
        } else {
            "/home/xlings/.xlings_data".to_string()
        }
    }

    pub fn xvm_datadir() -> String {
        if cfg!(target_os = "windows") {
            "C:/Users/Public/xlings/.xlings_data/xvm".to_string()
        } else if cfg!(target_os = "macos") {
            "/Users/xlings/.xlings_data/xvm".to_string()
        } else {
            "/home/xlings/.xlings_data/xvm".to_string()
        }
    }
}