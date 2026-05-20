
// 引入头文件
#include "main.h"

int main(){
    // 获取 libserialport 版本信息，验证库已正确链接
    struct sp_port **port_list;
    printf("==================================\n");
    printf("  SakuraCom - Serial Tool\n");
    printf("  libserialport imported\n");
    printf("==================================\n");

    // 列出可用串口
    sp_list_ports(&port_list);
    int port_count = 0;
    for (int i = 0; port_list[i] != NULL; i++) {
        port_count++;
        printf("  [%d] %s\n", i, sp_get_port_name(port_list[i]));
    }
    printf("  Found %d serial port(s)\n", port_count);
    sp_free_port_list(port_list);

    return 0;
}
