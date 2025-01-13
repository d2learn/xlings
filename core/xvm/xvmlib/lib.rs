mod config;
mod versiondb;
mod workspace;

use std::fs;
use std::sync::OnceLock;
use std::path::PathBuf;

use colored::*;

// public api

pub mod shims;
pub mod desktop;

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
    shims::create_shim_file(
        shims::XVM_ALIAS_WRAPPER,
        bindir,
        ""
    );
}

pub fn get_versiondb() -> &'static VersionDB {
    VERSION_DB.get().expect("VersionDB not initialized")
}

pub fn get_global_workspace() -> &'static Workspace {
    GLOBAL_WORKSPACE.get().expect("Global Workspace not initialized")
}

pub fn update_desktop_shortcut(program: &shims::Program) {

    let shortcut_name = format!("{} {}", program.name(), program.version());

    println!("update desktop shortcut for [{}]...", shortcut_name.green());

    let icon_path = if let Some(icon) = program.icon_path() {
        PathBuf::from(icon)
    } else {
        return;
    };

    if icon_path.exists() {
        let desktop_dir = desktop::shortcut_userdir();

        if !desktop_dir.exists() {
            println!("create desktop shortcut directory: {}", desktop_dir.display());
            fs::create_dir_all(&desktop_dir).expect("Failed to create desktop shortcut directory");
        }

        let exec_path = if let Some(epath) = program.bin_path() {
            PathBuf::from(epath)
        } else {
            // maybe is a alias
            return;
        };

        // check exec_path exists
        if exec_path.exists() {
            let options = desktop::ShortcutOptions {
                name: shortcut_name,
                exec_path: exec_path.clone(),
                icon_path: Some(icon_path),
                terminal: false,
                working_dir: Some(exec_path.parent().unwrap().to_path_buf()),
                description: None,
            };
            desktop::create_shortcut(options, &desktop_dir).expect("Failed to create desktop shortcut");
        } else {
            println!("Program not found: {}", exec_path.display());
        }
    } else {
        println!("Icon not found: {}", icon_path.display());
    }
}

pub fn remove_desktop_shortcut(target: &str, version: &str) {
    let desktop_dir = desktop::shortcut_userdir();
    desktop::delete_shortcut(&desktop_dir, &format!("{} {}", target, version)).expect("Failed to remove desktop shortcut");
}