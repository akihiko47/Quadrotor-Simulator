## Сборка на Linux

Установка зависимостей для скачивания и установки sdk
sudo apt-get update && sudo apt-get install -y libxcb-xinput0 libxcb-xinerama0 libxcb-cursor-dev

Установка SDK
wget -O vulkansdk-linux-x86_64-1.4.350.0.tar.xz \
  "https://sdk.lunarg.com/sdk/download/1.4.335.0/linux/vulkansdk-linux-x86_64-1.4.350.0.tar.xz"

Распоковать
sudo mkdir -p /opt/vulkan-sdk-1.4.350.0
sudo tar -xf vulkansdk-linux-x86_64-1.4.350.0.tar.xz -C /opt/vulkan-sdk-1.4.350.0 --strip-components=1

Переменные среды
скрипт setup-env.sh из sdk устанавливет такие переменные как VULKAN_SDK и VK_LAYER_PATH необходимые для работы программы
Добавляем в файл ~/.bashrc который выполняется каждый раз при запуске терминала
Теперь эти переменные не нужно настраивать вручную
echo 'source /opt/vulkan-sdk-1.4.350.0/x86_64/setup-env.sh' >> ~/.bashrc

Теперь можно перезапустить терминал или выполнить команду
source ~/.bashrc

Или можно выполнить скрипт
./setup_vulkan_sdk.sh 
и затем перезапустить терминал

Сборка
mkdir build
cd build
cmake ..

Перенести программу в корневую папку

Подробнее с установкой sdk можно ознакомиться по ссылке https://vulkan.lunarg.com/doc/view/latest/linux/getting_started.html

