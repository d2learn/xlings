extern crate xvmlib;

//use std::env;

use xvmlib::VersionDB;
use xvmlib::Workspace;

fn main() {

    let target = "python";
    //let current_dir = env::current_dir().unwrap();

    xvmlib::init_versiondb("config/versions.xvm.yaml");
    xvmlib::init_global_workspace("config/workspace.xvm.yaml");

    let mut wspace = Workspace::new("config/workspace.xvm.yaml").unwrap();
    let mut program = xvmlib::load_program_from_workspace(target, &wspace);
    program.add_arg("--version");
    program.run();
    wspace.set_active(true);
    wspace.set_version(target, "2.7.18");
    wspace.save_to_local().unwrap();

    let mut vdb = VersionDB::new("config/versions.xvm.yaml").unwrap();
    vdb.set_vdata("test2", "3.12.2", program.vdata());
    vdb.save_to_local().unwrap();
}