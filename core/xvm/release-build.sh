LINUX_RELEASE_DIR=target/release
WINDOWS_RELEASE_DIR=target/x86_64-pc-windows-gnu/release

cargo build --release
tar -czf $LINUX_RELEASE_DIR/xvm-dev-x86_64-linux-gnu.tar.gz $LINUX_RELEASE_DIR/xvm
cargo build --target x86_64-pc-windows-gnu --release
zip -r $WINDOWS_RELEASE_DIR/xvm-dev-x86_64-pc-windows-gnu.zip $WINDOWS_RELEASE_DIR/xvm.exe