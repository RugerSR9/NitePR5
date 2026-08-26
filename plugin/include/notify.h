#ifndef NITEPR5_NOTIFY_H
#define NITEPR5_NOTIFY_H

/* Classic toast only. Do not use libhijacker printf_notification or sceNotificationSend. */
void notify_toast(const char *message);
void notify_start(void);
void notify_armed(void);
void notify_dbg_missing(void);
void notify_dbg_recovered(void);

#endif
