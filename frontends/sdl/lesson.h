/** @file lesson.h
 *  @brief The hands-on tutorial: a lesson is a list of steps, each asking for
 *  one thing to be done on the instrument and ticking itself off when the
 *  front-end sees it done. The panel draws a step like a tour note
 *  (panel.cpp); main.cpp watches the input and moves the lesson on. */
#pragma once
#include <string>
#include "tour.h"

namespace gui
{

/** What a step waits for. */
struct LessonCheck
{
    enum Kind
    {
        Click,      /**< nothing: NEXT moves on (an explanation) */
        SwitchDown, /**< the mode switch is down */
        SwitchUp,   /**< the mode switch is up */
        Press,      /**< button a pressed */
        Hold,       /**< button a held for b milliseconds */
        Turn,       /**< encoder a turned b detents since the step began: b > 0 clockwise, b < 0 the other way */
        TurnAny,    /**< encoder a turned b detents either way since the step began */
        Push,       /**< encoder a's switch pressed b times since the step began */
        Keys,       /**< a different piano keys pressed since the step began */
    };
    /** Where the instrument must be for the action to count. */
    enum Where
    {
        Anywhere,
        SwitchIsDown, /**< the mode switch down */
        Menu,         /**< the mode switch down and CHOMPI held */
        SwitchIsUp,   /**< the mode switch up */
        ChompiUp,     /**< the mode switch up and CHOMPI held */
    };
    Kind  kind  = Click;
    int   a     = 0;
    int   b     = 0;
    Where where = Anywhere;
};

struct LessonStep
{
    TourTarget  target;
    const char* title;
    const char* body;
    const char* keys; /**< the simulator's keys for it, or nullptr */
    LessonCheck check;
};

struct Lesson
{
    const char*       name  = nullptr;
    const LessonStep* steps = nullptr;
    int               count = 0;
};

/** The lesson for a firmware id ("drone"); first steps on the instrument for any other. */
Lesson LessonFor(const std::string& firmware);

} // namespace gui
