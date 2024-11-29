一.同步分支到本地
# 1. 克隆您的仓库到本地（如果还没有的话）
git clone https://github.com/15255040419/xiaozhi-esp32.git
cd xiaozhi-esp32

# 2. 添加原仓库作为上游仓库
git remote add upstream https://github.com/78/xiaozhi-esp32.git

# 3. 获取原仓库的更新
git fetch upstream

# 4. 确保您在主分支上
git checkout main

# 5. 合并原仓库的更新
git merge upstream/main

# 6. 推送更新到您的GitHub仓库
git push origin main

# 同步所有标签
git fetch upstream --tags
git push origin --tags

二.修改后上传到github

# 1. 确保您在项目根目录
cd D:/FANG/xiaozhi/15255040419/xiaozhi-esp32

# 2. 创建并切换到新分支
git checkout -b lichuang-ranh

# 3. 添加新文件到git
git add main/boards/lichuang-ranh/config.h
git add main/boards/lichuang-ranh/lichuang_ranh_board.cc
git add main/Kconfig.projbuild
git add main/CMakeLists.txt

# 4. 提交更改
git commit -m "Add lichuang-ranh board based on lichuang-dev"

# 5. 推送到您的fork仓库
git push origin lichuang-ranh