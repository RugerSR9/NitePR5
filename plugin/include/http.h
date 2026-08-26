#ifndef NITEPR5_HTTP_H
#define NITEPR5_HTTP_H

/* HTTP/1.1 JSON on 0.0.0.0:1745. One thread; poll ~67 ms then freeze_tick if armed. */
void http_run(void);

#endif
