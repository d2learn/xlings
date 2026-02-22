use std::sync::OnceLock;
use std::path::PathBuf;
use std::env;

static RUNDIR: OnceLock<PathBuf> = OnceLock::new();

pub static WORKSPACE_FILE : &str = ".workspace.xvm.yaml";

#[allow(dead_code)]
pub fn rundir() {
    RUNDIR.get_or_init(|| {
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
    platform::xvm_homedir().join("bin").to_string_lossy().to_string()
}

#[allow(dead_code)]
pub fn libdir() -> String {
    platform::xvm_homedir().join("lib").to_string_lossy().to_string()
}

#[allow(dead_code)]
pub fn workspacedir() -> String {
    platform::xvm_homedir().to_string_lossy().to_string()
}

#[allow(dead_code)]
pub fn shimdir() -> String {
    format!("{}/shims", platform::xvm_datadir())
}

#[allow(dead_code)]
pub fn print_baseinfo() {
    println!("XVM Home: {}", platform::xvm_homedir().display());
    println!("XVM Data: {}", platform::xvm_datadir());
    println!("XVM Bindir: {}", bindir());
    println!("XVM VersionDB: {}", versiondb_file());
    println!("XVM Workspace: {}", workspace_file());
}

pub mod platform {
    use std::env;
    use std::path::PathBuf;

    fn user_home_dir() -> PathBuf {
        #[cfg(target_os = "windows")]
        {
            if let Ok(profile) = env::var("USERPROFILE") {
                return PathBuf::from(profile);
            }
            PathBuf::from(r"C:\Users\Public")
        }

        #[cfg(target_os = "macos")]
        {
            if let Ok(home) = env::var("HOME") {
                return PathBuf::from(home);
            }
            PathBuf::from("/tmp")
        }

        #[cfg(target_os = "linux")]
        {
            if let Ok(home) = env::var("HOME") {
                return PathBuf::from(home);
            }
            PathBuf::from("/tmp")
        }
    }

    /// XLINGS_DATA dir: env XLINGS_DATA > $HOME/.xlings/data
    pub fn xvm_homedir() -> PathBuf {
        if let Ok(data) = env::var("XLINGS_DATA") {
            return PathBuf::from(data);
        }
        let xlings_home = env::var("XLINGS_HOME")
            .map(PathBuf::from)
            .unwrap_or_else(|_| user_home_dir().join(".xlings"));
        xlings_home.join("data")
    }

    /// XVM data dir: $XLINGS_DATA/xvm
    pub fn xvm_datadir() -> String {
        xvm_homedir().join("xvm").to_string_lossy().to_string()
    }
}
