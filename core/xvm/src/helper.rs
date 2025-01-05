// helper
use std::io::{self, Write};
use std::fs;

use xvmlib::Workspace;

// try to load local workspace, if not found, load global workspace
pub fn load_workspace() -> Workspace {
    let mut workspace;
    if fs::metadata("workspace.xvm.yaml").is_ok() {
        workspace = load_local_workspace();
        if !workspace.active() {
            workspace = xvmlib::get_global_workspace().clone()
        }
    } else {
        workspace = xvmlib::get_global_workspace().clone()
    }
    workspace
}

pub fn load_local_workspace() -> Workspace {
    Workspace::from("workspace.xvm.yaml").expect("Failed to load Workspace")
}

pub fn load_workspace_and_merge() -> Workspace {
    let mut workspace = xvmlib::get_global_workspace().clone();
    if fs::metadata("workspace.xvm.yaml").is_ok() {
        let local_workspace = load_local_workspace();
        if local_workspace.active() {
            if local_workspace.inherit() {
                workspace.merge(&local_workspace);
                let new_wname = local_workspace.name().to_string() + " + global";
                workspace.set_name(&new_wname);
            } else {
                workspace = local_workspace;
            }
        }
    }
    workspace
}

pub fn prompt(prompt: &str, value: &str) -> bool {

    print!("{}", prompt);

    io::stdout().flush().unwrap();

    let mut input = String::new();
    io::stdin().read_line(&mut input).unwrap();
    let input = input.trim().to_lowercase();

    input == value
}