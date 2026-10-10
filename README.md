# WeChat Version Faker (Native C Edition)

轻量级（~20KB）Windows 微信 3.9.x 内存版本伪装加载器，解决老版本“当前微信版本过低，请升级至最新版本”强制登录拦截问题。

## 特点
- **纯原生 C 语言实现**：体积仅 20+ KB，启动耗时仅数毫秒，不依赖 Python 运行环境。
- **动态内存分页扫描**：基于 VirtualQueryEx 逐页过滤合法内存，无需硬编码偏移量。
- **纯内存热补丁**：不篡改硬盘文件，不破坏腾讯官方数字签名。

## 构建方法 (Linux 交叉编译)
```bash
sudo apt install mingw-w64
i686-w64-mingw32-gcc main.c -o fake_wechat_c.exe -O2 -s
```

