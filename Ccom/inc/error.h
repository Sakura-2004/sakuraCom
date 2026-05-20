
#ifndef H_ERROR_H
#define H_ERROR_H

/**
 *  @brief 此类维护了系统中一些提示信息
 */

/**
 * @brief 状态枚举
 * @param passTrue 正常
 * @param passFalsh 状态错误
 */
typedef enum error ERROR;
enum error{
    passTrue=10,
    passFalse=-10
};

/**
 * @brief 检查输入的值
 * @param e 输入的状态值
 * @param str 提示信息 如果输入NULL则成功不打印日志
 */
void checkError(ERROR e,const char* str);

/**
 * @brief 打印日志，支持 printf 风格的格式化输出
 * @param moduleID 模块标识符
 * @param format 格式化字符串，支持 printf 风格的格式化占位符
 * @param ... 可变参数，对应 format 中的占位符
 */
void printLog(const char* moduleID, const char* format, ...) __attribute__((format(printf, 2, 3)));


#endif