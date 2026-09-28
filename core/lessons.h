#pragma once
/* The homages' short history panels (drawn by ui_lesson): who, when, how the music was made, what to listen to. The
   texts are in core/lessons.c, in one place to check. The sentences come most important first: where a screen is too
   small, the panel keeps the first ones. */

struct lesson {
    const char *title;          /* the panel's title: "after …" */
    const char *text;           /* a paragraph */
    const char *how;            /* how the view does it */
    const char *listen;         /* works, " · " between them */
};

extern const struct lesson lesson_ans, lesson_meta, lesson_upic, lesson_reich, lesson_carlos, lesson_radigue, lesson_merzbow,
                           lesson_touch;
