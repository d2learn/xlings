// helper
use std::fs;

use xvmlib::Workspace;

pub fn get_workspace() -> Workspace {
    let mut workspace;
    if fs::metadata("workspace.xvm.yaml").is_ok() {
        workspace = get_local_workspace();
        if !workspace.active() {
            workspace = xvmlib::get_global_workspace().clone()
        }
    } else {
        workspace = xvmlib::get_global_workspace().clone()
    }
    workspace
}

pub fn get_local_workspace() -> Workspace {
    Workspace::from("workspace.xvm.yaml").expect("Failed to initialize Workspace")
}