extern crate xvmlib;

mod baseinfo;
mod helper;
mod cmdprocessor;

fn main() {
    xvmlib::init_versiondb("config/versions.xvm.yaml");
    xvmlib::init_global_workspace("config/workspace.xvm.yaml");
    let _ = cmdprocessor::run();
}