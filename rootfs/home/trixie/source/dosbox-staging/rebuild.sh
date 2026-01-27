cmake --build --preset=release-linux -j$(nproc)

echo "Installing to /usr/local/bin/dosbox..."
sudo install -m 755 build/release-linux/dosbox /usr/local/bin/dosbox
echo "Installing resources to /usr/local/share/dosbox-staging..."
sudo mkdir -p /usr/local/share/dosbox-staging
sudo cp -r build/release-linux/resources/* /usr/local/share/dosbox-staging/