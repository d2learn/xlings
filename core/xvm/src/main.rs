extern crate xvmlib;

use xvmlib::shims;

fn main() {
    let mut xim = shims::Program::new("node", "prev-0.0.2");
    //xim.add_env("PATH", "~/.nvm/versions/node/v21.7.3/bin");
    xim.add_args(&["-v", "o"]);
    xim.run();
}