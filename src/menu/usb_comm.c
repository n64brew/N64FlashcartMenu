/**
 * @file usb_comm.c
 * @brief USB communication component implementation
 * @ingroup ui_components
 */

// NOTE: This code doesn't implement EverDrive-64 USB protocol.
//       Main use of these functions is to aid menu development
//       (for example replace files on the SD card or reboot menu).

#include <stdio.h>
#include <string.h>

#include <fatfs/ff.h>
#include <usb.h>

#include "sound.h"
#include "usb_comm.h"
#include "utils/utils.h"

#define MAX_FILE_SIZE   MiB(4)

// File commands (list-dir, stat, free-space)
//
// These let a USB host manage the files on the SD card while the menu is running.
// Arguments are NUL terminated so paths may contain spaces.
// A command may be followed by an id of the host's choosing, as in "put-file:1234".
// Every command answers with any data in DATATYPE_RAWBINARY packets, followed by one
// DATATYPE_TEXT packet that repeats the command and id: "put-file:1234:ok\n" or
// "put-file:1234:error:<FatFs result code>\n". Debug output shares the channel, so this
// lets the host tell a reply from other text, and from the reply to an earlier request.

#define MAX_REQUEST_LENGTH (64)
#define MAX_PATH_LENGTH (512)
#define DATA_CHUNK_SIZE (4096) // Small enough to be sent well within the USB write timeout

/** @brief The supported USB commands structure. */
typedef struct {
    /** @brief The command identifier. */
    const char *id;

    /** @brief The command operation. */
    void (*op) (menu_t *menu);
} usb_comm_command_t;

/**
 * @brief Get a character from the USB input.
 * 
 * @return int The character read, or -1 if no character is available.
 */
static int usb_comm_get_char (void) {
    char c;

    if (USBHEADER_GETSIZE(usb_poll()) <= 0) {
        return -1;
    }

    usb_read(&c, sizeof(c));

    return (int) (c);
}

/**
 * @brief Read a string from the USB input.
 * 
 * @param string Buffer to store the string.
 * @param length Maximum length of the string.
 * @param end Character indicating the end of the string.
 * @return true if the string was read successfully, false otherwise.
 */
static bool usb_comm_read_string (char *string, int length, char end) {
    for (int i = 0; i < length; i++) {
        int c = usb_comm_get_char();

        if (c < 0) {
            return true;
        }

        string[i] = (char) (c);

        if (c == '\0' || c == end) {
            string[i] = '\0';
            break;
        }

        if (i == (length - 1)) {
            return true;
        }
    }

    return false;
}

/**
 * @brief Send an error message over USB.
 * 
 * @param message The error message.
 */
static void usb_comm_send_error (const char *message) {
    usb_purge();
    usb_write(DATATYPE_TEXT, message, strlen(message));
}

/**
 * @brief Reboot the system.
 * 
 * @param menu Pointer to the menu structure.
 */
static void command_reboot (menu_t *menu) {
    // The uploader overwrites SDRAM (including DFS) before sending this command;
    // stop audio immediately so the mixer stops reading from overwritten ROM data.
    sound_deinit();
    menu->next_mode = MENU_MODE_BOOT;

    menu->boot_params->device_type = BOOT_DEVICE_TYPE_ROM;
    menu->boot_params->tv_type = BOOT_TV_TYPE_PASSTHROUGH;
    menu->boot_params->detect_cic_seed = true;
    menu->boot_params->cheat_list = NULL;
    menu->boot_params->clear_rdram = false;
}

/**
 * @brief Receive a file over USB and save it to the storage.
 * 
 * @param menu Pointer to the menu structure.
 */
static void command_receive_file (menu_t *menu) {
    FILE *f;
    char buffer[256];
    uint8_t data[8192];
    char length[8];

    if (usb_comm_read_string(buffer, sizeof(buffer), ' ')) {
        return usb_comm_send_error("Invalid path argument\n");
    }

    if (usb_comm_get_char() != '@') {
        return usb_comm_send_error("Invalid argument\n");
    }

    if (usb_comm_read_string(length, sizeof(length), '@')) {
        return usb_comm_send_error("Invalid file length argument\n");
    }

    path_t *path = path_init(menu->storage_prefix, buffer);

    if ((f = fopen(path_get(path), "wb")) == NULL) {
        path_free(path);
        return usb_comm_send_error("Couldn't create file\n");
    }
    setbuf(f, NULL);
    path_free(path);

    int remaining = atoi(length);

    if (remaining > MAX_FILE_SIZE) {
        return usb_comm_send_error("File size too big\n");
    }

    while (remaining > 0) {
        int block_size = MIN(remaining, sizeof(data));
        usb_read(data, block_size);
        if (fwrite(data, 1, block_size, f) != block_size) {
            fclose(f);
            return usb_comm_send_error("Couldn't write all required data to the file\n");
        }
        remaining -= block_size;
    }

    if (fclose(f)) {
        return usb_comm_send_error("Couldn't flush data to the file\n");
    }

    if (usb_comm_get_char() != '\0') {
        return usb_comm_send_error("Invalid token at the end of data stream\n");
    }
}

/** @brief The command being handled, with its id if it was given one. */
static char request[MAX_REQUEST_LENGTH];

/**
 * @brief Send the result of a file command.
 *
 * @param result FatFs result code.
 */
static void usb_comm_send_result (FRESULT result) {
    char message[MAX_REQUEST_LENGTH + 32];
    int length = (result == FR_OK)
        ? sprintf(message, "%s:ok\n", request)
        : sprintf(message, "%s:error:%d\n", request, (int) (result));
    usb_purge();
    usb_write(DATATYPE_TEXT, message, length);
}

/**
 * @brief Format a directory entry as "attributes size date time name".
 *
 * @param buffer Buffer to store the line.
 * @param info FatFs file information.
 * @return int Length of the line.
 */
static int format_entry (char *buffer, FILINFO *info) {
    return sprintf(
        buffer,
        "%u %llu %u %u %s\n",
        info->fattrib,
        (unsigned long long) (info->fsize),
        info->fdate,
        info->ftime,
        info->fname
    );
}

/**
 * @brief List a directory.
 *
 * Arguments: <path>\0. The data is one line for each entry, in the format of format_entry().
 *
 * @param menu Pointer to the menu structure.
 */
static void command_list_dir (menu_t *menu) {
    char path[MAX_PATH_LENGTH];
    char data[DATA_CHUNK_SIZE];
    int length = 0;
    DIR dir;
    FILINFO info;

    if (usb_comm_read_string(path, sizeof(path), '\0')) {
        return usb_comm_send_result(FR_INVALID_PARAMETER);
    }
    usb_purge();

    FRESULT result = f_opendir(&dir, path);
    if (result != FR_OK) {
        return usb_comm_send_result(result);
    }

    while (((result = f_readdir(&dir, &info)) == FR_OK) && (info.fname[0] != '\0')) {
        if (length > (sizeof(data) - sizeof(info.fname) - 64)) {
            if (usb_write(DATATYPE_RAWBINARY, data, length) != 1) {
                f_closedir(&dir);
                return;
            }
            length = 0;
        }
        length += format_entry(data + length, &info);
    }

    f_closedir(&dir);

    if ((result == FR_OK) && (length > 0) && (usb_write(DATATYPE_RAWBINARY, data, length) != 1)) {
        return;
    }

    usb_comm_send_result(result);
}

/**
 * @brief Get the information about one file or directory.
 *
 * Arguments: <path>\0. The data is one line in the format of format_entry().
 *
 * @param menu Pointer to the menu structure.
 */
static void command_stat (menu_t *menu) {
    char path[MAX_PATH_LENGTH];
    char data[sizeof(((FILINFO *) (NULL))->fname) + 64];
    FILINFO info;

    if (usb_comm_read_string(path, sizeof(path), '\0')) {
        return usb_comm_send_result(FR_INVALID_PARAMETER);
    }
    usb_purge();

    FRESULT result = f_stat(path, &info);
    if ((result == FR_OK) && (usb_write(DATATYPE_RAWBINARY, data, format_entry(data, &info)) != 1)) {
        return;
    }

    usb_comm_send_result(result);
}

/**
 * @brief Get the size of the SD card and the free space.
 *
 * The data is one line: "<total bytes> <free bytes>".
 *
 * @param menu Pointer to the menu structure.
 */
static void command_free_space (menu_t *menu) {
    char data[64];
    FATFS *fs;
    DWORD free_clusters;

    usb_purge();

    FRESULT result = f_getfree("", &free_clusters, &fs);
    if (result == FR_OK) {
        uint64_t cluster_size = (uint64_t) (fs->csize) * FF_MAX_SS;
        int length = sprintf(
            data,
            "%llu %llu\n",
            (unsigned long long) ((fs->n_fatent - 2) * cluster_size),
            (unsigned long long) (free_clusters * cluster_size)
        );
        if (usb_write(DATATYPE_RAWBINARY, data, length) != 1) {
            return;
        }
    }

    usb_comm_send_result(result);
}

static usb_comm_command_t commands[] = {
    { .id = "reboot", .op = command_reboot },
    { .id = "send-file", .op = command_receive_file }, // Note that this is a crossover with the `id` related to the PC commands.
    { .id = "list-dir", .op = command_list_dir },
    { .id = "stat", .op = command_stat },
    { .id = "free-space", .op = command_free_space },
    { .id = NULL },
};

/**
 * @brief Poll the USB input for commands.
 * 
 * @param menu Pointer to the menu structure.
 */
void usb_comm_poll (menu_t *menu) {
    uint32_t header = usb_poll();

    if (USBHEADER_GETTYPE(header) != DATATYPE_TEXT) {
        usb_purge();
        return;
    }

    if (USBHEADER_GETSIZE(header) > 0) {
        if (usb_comm_read_string(request, sizeof(request), ' ')) {
            usb_comm_send_error("Command id too long\n");
        } else {
            usb_comm_command_t *cmd = commands;

            // The command ends at the ':' that starts an optional request id
            size_t cmd_id_length = strcspn(request, ":");

            while (cmd->id != NULL) {
                if ((strlen(cmd->id) == cmd_id_length) && (strncmp(cmd->id, request, cmd_id_length) == 0)) {
                    cmd->op(menu);
                    break;
                }
                cmd++;
            }

            usb_purge();

            if (cmd->id == NULL) {
                usb_comm_send_error("Unknown command\n");
            }
        }
    }
}
