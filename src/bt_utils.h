//
// Created by willyy on 2025-12-12.
//

#ifndef VS1984_UTILITY_H
#define VS1984_UTILITY_H
#ifdef __cplusplus
extern "C" {
#endif
const char *rsx_version();
const char *build_version();
void set_debug(int dbg);
void ilogs(const char *format, ...);
void ilogi(const char *format, ...);
void ilogii(const char *format, ...);
void ilogiii(const char *format, ...);
void iloge(const char *format, ...);
#ifdef __cplusplus
}
#endif
#endif //VS1984_UTILITY_H