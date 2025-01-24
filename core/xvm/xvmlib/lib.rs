mod config;
mod versiondb;
mod workspace;

use std::fs;
use std::sync::OnceLock;

use colored::*;

// public api

pub mod shims;

pub use versiondb::VersionDB;
pub use workspace::Workspace;

// read-only global state
static VERSION_DB: OnceLock<VersionDB> = OnceLock::new();
static GLOBAL_WORKSPACE: OnceLock<Workspace> = OnceLock::new();

pub fn init_versiondb(yaml_file: &str) {
    VERSION_DB.get_or_init(|| {

        if fs::metadata(yaml_file).is_err() {
            println!("init_versiondb: create {}", yaml_file);
            return VersionDB::new(yaml_file);
        }

        //println!("init_versiondb: load from file");

        VersionDB::from(yaml_file).expect("Failed to initialize VersionDB")
    });
}

pub fn init_global_workspace(yaml_file: &str) {
    GLOBAL_WORKSPACE.get_or_init(|| {
        if fs::metadata(yaml_file).is_err() {
            return Workspace::new(yaml_file, "global");
        }

        let workspace = Workspace::from(yaml_file).expect("Failed to initialize Workspace");

        if !workspace.active() {
            println!(
                "\n\t{}\n",
                "WARNING: Global Workspace is not active. Run 'xvm workspace global --active true' to activate it."
                .yellow().bold()
            );
        }

        workspace
    });
}

pub fn init_shims(bindir : &str) {
    shims::init(bindir);
}

pub fn get_versiondb() -> &'static VersionDB {
    VERSION_DB.get().expect("VersionDB not initialized")
}

pub fn get_global_workspace() -> &'static Workspace {
    GLOBAL_WORKSPACE.get().expect("Global Workspace not initialized")
}