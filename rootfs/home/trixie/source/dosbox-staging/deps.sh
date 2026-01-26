cd ~/source
git clone https://github.com/berndporr/iir1.git
cd iir1
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
sudo make install
sudo ldconfig

cd ~/source

git clone https://github.com/munt/munt.git
cd munt/mt32emu
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j4
sudo make install
sudo ldconfig
