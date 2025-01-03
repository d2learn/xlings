mod config;
mod versiondb;
mod workspace;

// public api

pub mod shims;

use std::sync::OnceLock;

pub use versiondb::VersionDB;
pub use workspace::Workspace;

pub static VERSION_DB: OnceLock<VersionDB> = OnceLock::new();
pub static GLOBAL_WORKSPACE: OnceLock<Workspace> = OnceLock::new();

pub fn init_versiondb(yaml_file: &str) {
    VERSION_DB.get_or_init(|| {
        VersionDB::new(yaml_file).expect("Failed to initialize VersionDB")
    });
}

pub fn get_versiondb() -> &'static VersionDB {
    VERSION_DB.get().expect("VersionDB not initialized")
}

pub fn init_global_workspace(yaml_file: &str) {
    GLOBAL_WORKSPACE.get_or_init(|| {
        Workspace::new(yaml_file).expect("Failed to initialize Workspace")
    });
}

pub fn get_global_workspace() -> &'static Workspace {
    GLOBAL_WORKSPACE.get().expect("Global Workspace not initialized")
}

pub fn load_program(target: &str) -> shims::Program {
    let global_workspace = get_global_workspace();

    if !global_workspace.active() {
        panic!("Global workspace is not active");
    }

    load_program_from_workspace(target, &global_workspace)
}

pub fn load_program_from_workspace(target: &str, workspace: &Workspace) -> shims::Program {

    if !workspace.active() {
        return load_program(target);
    }

    let version = workspace
        .version(target)
        .or_else(|| get_global_workspace().version(target))
        .expect("Version not found");

    let vdata = get_versiondb()
        .get_vdata(target, version)
        .expect("Version data not found");

    let mut program = shims::Program::new(target, version);
    program.set_vdata(vdata);

    program
}