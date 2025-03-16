// 在初始化显示器后创建开机动画
BootAnimation* boot_animation = new BootAnimation(lv_scr_act());
boot_animation->Show();

// 在适当的时机（例如网络连接成功后）隐藏开机动画
// boot_animation->Hide();

// 不要忘记在适当的时机释放资源
// delete boot_animation; 