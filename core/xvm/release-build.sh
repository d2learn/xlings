LINUX_RELEASE_DIR=`pwd`/target/release
WINDOWS_RELEASE_DIR=`pwd`/target/x86_64-pc-windows-gnu/release

cargo build --release
cargo build --target x86_64-pc-windows-gnu --release
cd $LINUX_RELEASE_DIR
tar -czf xvm-dev-x86_64-linux-gnu.tar.gz xvm
cd $WINDOWS_RELEASE_DIR
zip -r xvm-dev-x86_64-pc-windows-gnu.zip xvm.exe