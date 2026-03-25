#include "app.h"
#include "argparse.h"
#include "log.h"
#include "midi.h"
#include "version.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#define MAX_TITLE_LEN 256

SDL_AppResult SDL_AppInit(void **appstate, int argc, char **argv) {
    log_init("app.log.jsonl", LOG_DEBUG);

    ArgParse *ap = malloc(sizeof(ArgParse));
    CHECK_ERR_SDL(argparse_init(ap, "C-Synth", NULL, NULL, 0));

    SDL_AppResult res = argparse_parse(argc, argv, ap);
    if (res != SDL_APP_CONTINUE) {
        return res;
    }

    App_t *app = malloc(sizeof(App_t));
    app->ap = ap;

    char title[MAX_TITLE_LEN];
    sprintf(title, "Version: %s (%s)%s, built: %s", PROJECT_GIT_DESCRIBE,
            PROJECT_GIT_COMMIT, PROJECT_GIT_DIRTY ? "-dirty" : "",
            PROJECT_BUILD_TIMESTAMP);
    SDL_Window *window =
        SDL_CreateWindow(title, 800, 600, SDL_WINDOW_FULLSCREEN);

    if (!window)
        return SDL_APP_FAILURE;

    app->window = window;

    PmError err;
    err = Pm_Initialize();
    if (err != pmNoError) {
        fprintf(stderr, "Pm_Initialize failed: %s\n", Pm_GetErrorText(err));
        return 1;
    }

    int num_devs = Pm_CountDevices();
    if (num_devs <= 0) {
        fprintf(stderr, "No MIDI devices found\n");
        Pm_Terminate();
        goto failure;
    }

    const PmDeviceInfo *info;
    int input_id = -1;
    for (int i = 0; i < num_devs; ++i) {
        info = Pm_GetDeviceInfo(i);
        if (info && info->input) {
            input_id = i;
            break;
        }
    }
    if (input_id < 0) {
        fprintf(stderr, "No MIDI input devices available\n");
        Pm_Terminate();
        goto failure;
    }

    PmStream *stream;
    err = Pm_OpenInput(&stream, input_id, NULL, 512, NULL, NULL);
    if (err != pmNoError) {
        fprintf(stderr, "Pm_OpenInput failed: %s\n", Pm_GetErrorText(err));
        Pm_Terminate();
        goto failure;
    }

    printf("Opened input: %s\n", Pm_GetDeviceInfo(input_id)->name);

    app->stream = stream;

    *appstate = app;

    return SDL_APP_CONTINUE;

failure:
    SDL_DestroyWindow(app->window);
    free(app);
    return SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    App_t *app = (App_t *)appstate;
    PmEvent buffer[32];
    int count = Pm_Read(app->stream, buffer, 32);
    if (count < 0) {
        fprintf(stderr, "Pm_Read error: %d\n", count);
        return SDL_APP_FAILURE;
    }
    for (int i = 0; i < count; ++i) {
        midi_message_unpacked_t msg = midi_from_uint32(buffer[i].message).msg;

        printf("message: data1: %u, data2: %u, padding: %u, status: %u\n",
               msg.data1, msg.data2, msg._pad, msg.status.raw);

        switch (msg.status.type) {
        case 0x8:
            printf("Note Off  ch=%u note=%u vel=%u\n", msg.status.channel + 1,
                   msg.data1, msg.data2);
            break;
        case 0x9:
            printf("Note On   ch=%u note=%u vel=%u\n", msg.status.channel + 1,
                   msg.data1, msg.data2);
            break;
        case 0xB:
            printf("Control    ch=%u ctrl=%u val=%u\n", msg.status.channel + 1,
                   msg.data1, msg.data2);
            break;
        case 0xC:
            printf("ProgramCh  ch=%u prog=%u\n", msg.status.channel + 1,
                   msg.data1);
            break;
        default:
            printf("Other msg  status=0x%02X ch=%u d1=%u d2=%u\n",
                   msg.status.type, msg.status.channel + 1, msg.data1,
                   msg.data2);
        }
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    (void)appstate;

    char buf[256];
    SDL_GetEventDescription(event, buf, sizeof(buf));
    printf("Received event: %s\n", buf);

    switch (event->type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
    default:
        break;
    }
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    (void)appstate;
    (void)result;
}

/*int main(void) {
    char title[MAX_TITLE_LEN];
    sprintf(title, "Version: %s (%s)%s, built: %s", PROJECT_GIT_DESCRIBE,
            PROJECT_GIT_COMMIT, PROJECT_GIT_DIRTY ? "-dirty" : "",
            PROJECT_BUILD_TIMESTAMP);
    printf("%s\n",title);

    PmError err;
    err = Pm_Initialize();
    if (err != pmNoError) {
        fprintf(stderr, "Pm_Initialize failed: %s\n", Pm_GetErrorText(err));
        return 1;
    }

    int num_devs = Pm_CountDevices();
    if (num_devs <= 0) {
        fprintf(stderr, "No MIDI devices found\n");
        Pm_Terminate();
        return 1;
    }

    const PmDeviceInfo *info;
    int input_id = -1;
    for (int i = 0; i < num_devs; ++i) {
        info = Pm_GetDeviceInfo(i);
        if (info && info->input) {
            input_id = i;
            break;
        }
    }
    if (input_id < 0) {
        fprintf(stderr, "No MIDI input devices available\n");
        Pm_Terminate();
        return 1;
    }

    PmStream *stream;
    err = Pm_OpenInput(&stream, input_id, NULL, 512, NULL, NULL);
    if (err != pmNoError) {
        fprintf(stderr, "Pm_OpenInput failed: %s\n", Pm_GetErrorText(err));
        Pm_Terminate();
        return 1;
    }

    printf("Opened input: %s\n", Pm_GetDeviceInfo(input_id)->name);

    UI *ui = ui_init(title, 800, 600);
    if (!ui) goto end;

    printf("starting loop\n");
    while (ui->running) {
        ui_poll_events(ui);
        ui_clear(ui, 30, 30, 30, 255);
        // draw here using ui->renderer, e.g. a white rectangle
        //SDL_SetRenderDrawColor(ui->renderer, 255, 255, 255, 255);
        //SDL_FRect r = {ui->width / 4, ui->height / 4, ui->width / 2,
        //              ui->height / 2};
        //SDL_RenderFillRect(ui->renderer, &r);
        ui_present(ui);

        PmEvent buffer[32];
        int count = Pm_Read(stream, buffer, 32);
        if (count < 0) {
            fprintf(stderr, "Pm_Read error: %d\n", count);
            break;
        }
        for (int i = 0; i < count; ++i) {
            midi_note_t msg = {.msg = (uint32_t)buffer[i].message};

            printf("message: data1: %u, data2: %u, data3: %u, status: %u\n",
                   msg.data1, msg.data2, msg.data3, msg.status);

            switch (msg.type) {
            case 0x8:
                printf("Note Off  ch=%u note=%u vel=%u\n", msg.channel + 1,
                       msg.data1, msg.data2);
                break;
            case 0x9:
                printf("Note On   ch=%u note=%u vel=%u\n", msg.channel + 1,
                       msg.data1, msg.data2);
                break;
            case 0xB:
                printf("Control    ch=%u ctrl=%u val=%u\n", msg.channel + 1,
                       msg.data1, msg.data2);
                break;
            case 0xC:
                printf("ProgramCh  ch=%u prog=%u\n", msg.channel + 1,
                       msg.data1);
                break;
            default:
                printf("Other msg  status=0x%02X ch=%u d1=%u d2=%u\n", msg.type,
                       msg.channel + 1, msg.data1, msg.data2);
            }
        }
        Pt_Sleep(10);
        // SDL_Delay(16); // ~60 fps cap
    }

end:
    ui_destroy(ui);

    Pm_Close(stream);
    Pm_Terminate();
    return 0;
}*/
