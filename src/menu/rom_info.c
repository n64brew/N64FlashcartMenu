/**
 * @file rom_info.c
 * @brief ROM Information component implementation
 * @ingroup menu
 */


#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <libdragon.h>
#include <miniz.h>

#include "ini_parser.h"

#include "boot/cic.h"
#include "rom_info.h"
#include "utils/fs.h"


#define SWAP_VARS(x0, x1)       { typeof(x0) tmp = (x0); (x0) = (x1); (x1) = (tmp); }

#define PI_CONFIG_BIG_ENDIAN    (0x80371240)
#define PI_CONFIG_LITTLE_ENDIAN (0x40123780)
#define PI_CONFIG_BYTE_SWAPPED  (0x37804012)
#define PI_CONFIG_64DD_IPL      (0x80270740)

#define CLOCK_RATE_DEFAULT      (0x0000000F)
#define ROM_METADATA_INI_MAX_SIZE (64u * 1024u)


/** @brief ROM File Information Structure. */
typedef struct  __attribute__((packed)) {
    uint32_t pi_dom1_config;
    uint32_t clock_rate;
    uint32_t boot_address;
    struct {
        uint8_t __unused_1[2];
        uint8_t version;
        char revision;
    } libultra;
    uint64_t check_code;
    uint8_t __unused_1[8];
    char title[20];
    uint8_t __unused_2[7];
    union {
        char game_code[4];
        struct {
            char category_code;
            char unique_code[2];
            char destination_code;
        };
    };
    uint8_t version;
    uint8_t ipl3[IPL3_LENGTH];
} rom_header_t;

/** @brief ROM Information Match Type Enumeration. */
typedef enum {
    MATCH_TYPE_ID, /**< Check only game code */
    MATCH_TYPE_ID_REGION, /**< Check game code and region */
    MATCH_TYPE_ID_REGION_VERSION, /**< Check game code, region and version */
    MATCH_TYPE_CHECK_CODE, /**< Check game check code */
    MATCH_TYPE_HOMEBREW_HEADER, /**< Check for homebrew header ID */
    MATCH_TYPE_END /**< List end marker */
} match_type_t;

/** @brief ROM Features Enumeration. */
typedef enum {
    FEAT_NONE = 0, /**< No features supported */
    FEAT_CPAK = (1 << 0), /**< Controller Pak */
    FEAT_RPAK = (1 << 1), /**< Rumble Pak */
    FEAT_TPAK = (1 << 2), /**< Transfer Pak */
    FEAT_VRU = (1 << 3), /**< Voice Recognition Unit */
    FEAT_RTC = (1 << 4), /**< Real Time Clock */
    FEAT_EXP_PAK_REQUIRED = (1 << 5), /**< Expansion Pak required */
    FEAT_EXP_PAK_RECOMMENDED = (1 << 6), /**< Expansion Pak recommended */
    FEAT_EXP_PAK_UNUSED = (1 << 7), /**< Expansion Pak unused */
    FEAT_EXP_PAK_BROKEN = (1 << 8), /**< Expansion Pak broken */
    FEAT_64DD_CONVERSION = (1 << 9), /**< 64DD disk to ROM conversion */
    FEAT_64DD_ENHANCED = (1 << 10) /**< Combo ROM + Disk games */
} feat_t;

/** @brief ROM Match Structure. */
typedef struct {
    match_type_t type; /**< Match type */
    union {
        struct {
            const char *id; /**< Game code or unique ID */
            uint8_t version; /**< Game version */
        };
        uint64_t check_code; /**< Game check code */
    } fields;
    struct {
        rom_save_type_t save; /**< Save type */
        feat_t feat; /**< Supported features */
    } data;
} match_t;

#define MATCH_ID(i, s, f)                       { .type = MATCH_TYPE_ID, .fields = { .id = i }, .data = { .save = s, .feat = f } }
#define MATCH_ID_REGION(i, s, f)                { .type = MATCH_TYPE_ID_REGION, .fields = { .id = i }, .data = { .save = s, .feat = f } }
#define MATCH_ID_REGION_VERSION(i, v, s, f)     { .type = MATCH_TYPE_ID_REGION_VERSION, .fields = { .id = i, .version = v }, .data = { .save = s, .feat = f } }
#define MATCH_CHECK_CODE(c, s, f)               { .type = MATCH_TYPE_CHECK_CODE, .fields = { .check_code = c }, .data = { .save = s, .feat = f } }
#define MATCH_HOMEBREW_HEADER(i)                { .type = MATCH_TYPE_HOMEBREW_HEADER, .fields = { .id = i }, .data = { .feat = FEAT_NONE } }
#define MATCH_END                               { .type = MATCH_TYPE_END, .data = { .save = SAVE_TYPE_NONE, .feat = FEAT_NONE } }


// List shamelessly stolen from https://github.com/ares-emulator/ares/blob/master/mia/medium/nintendo-64.cpp

// clang-format off
static const match_t database[] = {
    MATCH_HOMEBREW_HEADER("ED"),                                                                                // Homebrew header (ED)

    MATCH_CHECK_CODE(0x000000004CBC3B56, SAVE_TYPE_SRAM_256KBIT, FEAT_EXP_PAK_REQUIRED | FEAT_64DD_CONVERSION), // DMTJ 64DD cartridge conversion

    MATCH_CHECK_CODE(0x0DD4ABABB5A2A91E, SAVE_TYPE_EEPROM_16KBIT, FEAT_EXP_PAK_REQUIRED),                       // DK Retail kiosk demo
    MATCH_CHECK_CODE(0xEB85EBC9596682AF, SAVE_TYPE_FLASHRAM_1MBIT, FEAT_NONE),                                  // Doubutsu Banchou
    MATCH_CHECK_CODE(0x9A746EBF2802EA99, SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                    // Toon panic
    MATCH_CHECK_CODE(0x21548CA921548CA9, SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                    // Mini racers
    MATCH_CHECK_CODE(0xBC9B2CC34ED04DA5, SAVE_TYPE_FLASHRAM_1MBIT, FEAT_NONE),                                  // Starcraft 64 [Prototype 2000]
    MATCH_CHECK_CODE(0x5D40ED2C10D6ABCF, SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                    // Viewpoint 2064
    MATCH_CHECK_CODE(0x7280E03F497689BA, SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                    // Viewpoint 2064 [ENG patch]

    MATCH_CHECK_CODE(0xCDB8B4D08832352D, SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                    // Jet Force Gemini [USA CRACK]
    MATCH_CHECK_CODE(0xB66E0F7C2709C22F, SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                    // Jet Force Gemini [PAL CRACK]

    MATCH_CHECK_CODE(0xCE84793D27ECC1AD, SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK | FEAT_EXP_PAK_REQUIRED),            // Donkey kong 64 [USA CRACK]
    MATCH_CHECK_CODE(0x1F95CAAA047FC22A, SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK | FEAT_EXP_PAK_REQUIRED),            // Donkey kong 64 [PAL CRACK]

    MATCH_CHECK_CODE(0xE3FF09DFCAE4B0ED, SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                    // Banjo tooie [USA CRACK]
    MATCH_CHECK_CODE(0xAE6B1E11B7CBD69E, SAVE_TYPE_NONE, FEAT_CPAK),                                            // Dragon Sword [Prototype]

    MATCH_ID_REGION_VERSION("NK4J", 0, SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                      // Kirby 64: The Crystal Shards [Hoshi no Kirby 64 (J)]
    MATCH_ID_REGION_VERSION("NK4J", 1, SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                      // Kirby 64: The Crystal Shards [Hoshi no Kirby 64 (J)]
    MATCH_ID("NK4", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Kirby 64: The Crystal Shards [Hoshi no Kirby 64 (J)]

    MATCH_ID_REGION_VERSION("NSMJ", 3, SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                      // Super Mario 64 Shindou Edition
    MATCH_ID("NSM", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Super Mario 64

    MATCH_ID_REGION_VERSION("NWRJ", 2, SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                          // Wave Race 64 Shindou Edition
    MATCH_ID("NWR", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Wave Race 64

    MATCH_ID_REGION("N3HJ", SAVE_TYPE_SRAM_256KBIT, FEAT_NONE),                                                 // Ganbare! Nippon! Olympics 2000
    MATCH_ID("N3H", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // International Track & Field 2000

    MATCH_ID_REGION("ND3J", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                // Akumajou Dracula Mokushiroku (J)
    MATCH_ID("ND3", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Castlevania

    MATCH_ID_REGION("ND4J", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                // Akumajou Dracula Mokushiroku Gaiden: Legend of Cornell (J)
    MATCH_ID("ND4", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Castlevania - Legacy of Darkness

    MATCH_ID_REGION("NDKJ", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                 // Dark Rift [Space Dynamites (J)]

    MATCH_ID_REGION("NPDJ", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK | FEAT_RPAK | FEAT_TPAK | FEAT_EXP_PAK_REQUIRED),// Perfect Dark (J)
    MATCH_ID("NPD", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK | FEAT_RPAK | FEAT_TPAK | FEAT_EXP_PAK_RECOMMENDED),     // Perfect Dark

    MATCH_ID_REGION("NSVE", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                 // Space Station Silicon Valley
    MATCH_ID("NSV", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK | FEAT_EXP_PAK_BROKEN),                                   // Space Station Silicon Valley

    MATCH_ID_REGION("NWTJ", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                 // Wetrix
    MATCH_ID("NWT", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Wetrix

    // Aleck64 conversions
    MATCH_ID("ZSE", SAVE_TYPE_NONE, FEAT_EXP_PAK_REQUIRED),                                                     // Super Real Mahjong VS (Aleck64 conversion)
    MATCH_ID("ZSA", SAVE_TYPE_NONE, FEAT_EXP_PAK_REQUIRED),                                                     // Vivid Dolls (Aleck64 conversion)


    // EEPROM 4K
    MATCH_ID("CLB", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK | FEAT_64DD_ENHANCED),                                    // Mario Party (NTSC)
    MATCH_ID("NAB", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Air Boarder 64
    MATCH_ID("NAD", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Worms Armageddon (U)
    MATCH_ID("NAG", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // AeroGauge
    MATCH_ID("NB6", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_TPAK),                                             // Super B-Daman: Battle Phoenix 64
    MATCH_ID("NBC", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Blast Corps
    MATCH_ID("NBD", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Bomberman Hero [Mirian Ojo o Sukue! (J)]
    MATCH_ID("NBH", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Body Harvest
    MATCH_ID("NBK", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Banjo-Kazooie [Banjo to Kazooie no Daiboken (J)]
    MATCH_ID("NBM", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Bomberman 64 [Baku Bomberman (J)]
    MATCH_ID("NBN", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Bakuretsu Muteki Bangaioh
    MATCH_ID("NBV", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Bomberman 64: The Second Attack! [Baku Bomberman 2 (J)]
    MATCH_ID("NCG", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK | FEAT_TPAK),                                 // Choro Q 64 II - Hacha Mecha Grand Prix Race (J)
    MATCH_ID("NCH", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Chopper Attack
    MATCH_ID("NCR", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Penny Racers [Choro Q 64 (J)]
    MATCH_ID("NCT", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Chameleon Twist
    MATCH_ID("NCU", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Cruis'n USA
    MATCH_ID("NCX", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Custom Robo
    MATCH_ID("NDQ", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Disney's Donald Duck - Goin' Quackers [Quack Attack (E)]
    MATCH_ID("NDR", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Doraemon: Nobita to 3tsu no Seireiseki
    MATCH_ID("NDU", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Duck Dodgers starring Daffy Duck
    MATCH_ID("NDY", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Diddy Kong Racing
    MATCH_ID("NEA", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // PGA European Tour
    MATCH_ID("NER", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Aero Fighters Assault [Sonic Wings Assault (J)]
    MATCH_ID("NF2", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // F-1 World Grand Prix II
    MATCH_ID("NFG", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Fighter Destiny 2
    MATCH_ID("NFH", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // In-Fisherman Bass Hunter 64
    MATCH_ID("NFW", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // F-1 World Grand Prix
    MATCH_ID("NFX", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Star Fox 64 [Lylat Wars (E)]
    MATCH_ID("NFY", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Kakutou Denshou: F-Cup Maniax
    MATCH_ID("NGE", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // GoldenEye 007
    MATCH_ID("NGL", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Getter Love!!
    MATCH_ID("NGU", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Tsumi to Batsu: Hoshi no Keishousha (Sin and Punishment)
    MATCH_ID("NGV", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Glover
    MATCH_ID("NHA", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Bomberman 64: Arcade Edition (J)
    MATCH_ID("NHF", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // 64 Hanafuda: Tenshi no Yakusoku
    MATCH_ID("NHP", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Heiwa Pachinko World 64
    MATCH_ID("NIC", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Indy Racing 2000
    MATCH_ID("NIJ", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK | FEAT_EXP_PAK_RECOMMENDED),                              // Indiana Jones and the Infernal Machine
    MATCH_ID("NIR", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Utchan Nanchan no Hono no Challenger: Denryuu Ira Ira Bou
    MATCH_ID("NJK", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Viewpoint 2064 (Final Prototype) (J)
    MATCH_ID("NJM", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Earthworm Jim 3D
    MATCH_ID("NK2", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Snowboard Kids 2 [Chou Snobow Kids (J)]
    MATCH_ID("NKA", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Fighters Destiny [Fighting Cup (J)]
    MATCH_ID("NKI", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Killer Instinct Gold
    MATCH_ID("NKT", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Mario Kart 64
    MATCH_ID("NLB", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Mario Party (PAL)
    MATCH_ID("NLL", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Last Legion UX
    MATCH_ID("NLR", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Lode Runner 3-D
    MATCH_ID("NMG", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Monaco Grand Prix [Racing Simulation 2 (G)]
    MATCH_ID("NMI", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Mission: Impossible
    MATCH_ID("NML", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK | FEAT_TPAK),                                             // Mickey's Speedway USA [Mickey no Racing Challenge USA (J)]
    MATCH_ID("NMO", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Monopoly
    MATCH_ID("NMR", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Multi-Racing Championship
    MATCH_ID("NMS", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // Morita Shougi 64
    MATCH_ID("NMU", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Big Mountain 2000
    MATCH_ID("NMW", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Mario Party 2
    MATCH_ID("NMZ", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Zool - Majou Tsukai Densetsu (J)
    MATCH_ID("NN6", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Dr. Mario 64
    MATCH_ID("NNA", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Star Wars Episode I: Battle for Naboo
    MATCH_ID("NOS", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // 64 Oozumou
    MATCH_ID("NO2", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // 64 Oozumou 2
    MATCH_ID("NP2", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Chou Kuukan Night Pro Yakyuu King 2 (J)
    MATCH_ID("NPG", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK | FEAT_VRU),                                              // Hey You, Pikachu! [Pikachu Genki Dechu (J)]
    MATCH_ID("NPT", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK | FEAT_TPAK),                                             // Puyo Puyon Party
    MATCH_ID("NPW", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Pilotwings 64
    MATCH_ID("NPY", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Puyo Puyo Sun 64
    MATCH_ID("NRA", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Rally '99 (J)
    MATCH_ID("NRC", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Top Gear Overdrive
    MATCH_ID("NRS", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Star Wars: Rogue Squadron [Shutsugeki! Rogue Chuutai (J)]
    MATCH_ID("NS3", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // AI Shougi 3
    MATCH_ID("NS6", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Star Soldier: Vanishing Earth
    MATCH_ID("NSA", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Sonic Wings Assault (J)
    MATCH_ID("NSC", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Starshot: Space Circus Fever
    MATCH_ID("NSN", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Snow Speeder (J)
    MATCH_ID("NSS", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Super Robot Spirits
    MATCH_ID("NSU", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Rocket: Robot on Wheels
    MATCH_ID("NSW", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Star Wars: Shadows of the Empire [Teikoku no Kage (J)]
    MATCH_ID("NT6", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Tetris 64
    MATCH_ID("NTB", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Transformers: Beast Wars Metals 64 (J)
    MATCH_ID("NTC", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // 64 Trump Collection
    MATCH_ID("NTJ", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Tom & Jerry in Fists of Fury
    MATCH_ID("NTM", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Mischief Makers [Yuke Yuke!! Trouble Makers (J)]
    MATCH_ID("NTN", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // All Star Tennis '99
    MATCH_ID("NTP", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Tetrisphere
    MATCH_ID("NTR", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Top Gear Rally (J + E)
    MATCH_ID("NTW", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK),                                                         // 64 de Hakken!! Tamagotchi
    MATCH_ID("NTX", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Taz Express
    MATCH_ID("NVL", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // V-Rally Edition '99
    MATCH_ID("NVY", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // V-Rally Edition '99 (J)
    MATCH_ID("NWC", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Wild Choppers
    MATCH_ID("NWQ", SAVE_TYPE_EEPROM_4KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Rally Challenge 2000
    MATCH_ID("NWU", SAVE_TYPE_EEPROM_4KBIT, FEAT_NONE),                                                         // Worms Armageddon (E)
    MATCH_ID("NXO", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Cruis'n Exotica
    MATCH_ID("NYK", SAVE_TYPE_EEPROM_4KBIT, FEAT_RPAK),                                                         // Yakouchuu II: Satsujin Kouro

    // EEPROM 16K
    MATCH_ID("N3D", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Doraemon 3: Nobita no Machi SOS!
    MATCH_ID("NB7", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Banjo-Tooie [Banjo to Kazooie no Daiboken 2 (J)]
    MATCH_ID("NCW", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Cruis'n World
    MATCH_ID("NCZ", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Custom Robo V2
    MATCH_ID("ND2", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Doraemon 2: Nobita to Hikari no Shinden
    MATCH_ID("ND6", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK | FEAT_VRU),                                             // Densha de Go! 64
    MATCH_ID("NDO", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK | FEAT_EXP_PAK_REQUIRED),                                // Donkey Kong 64
    MATCH_ID("NEP", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Star Wars Episode I: Racer
    MATCH_ID("NEV", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Neon Genesis Evangelion
    MATCH_ID("NFU", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Conker's Bad Fur Day
    MATCH_ID("NGC", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK | FEAT_RPAK),                                            // GT 64: Championship Edition
    MATCH_ID("NGT", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK | FEAT_RPAK),                                            // City Tour GrandPrix - Zen Nihon GT Senshuken
    MATCH_ID("NIM", SAVE_TYPE_EEPROM_16KBIT, FEAT_NONE),                                                        // Ide Yosuke no Mahjong Juku
    MATCH_ID("NM8", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK | FEAT_TPAK),                                            // Mario Tennis
    MATCH_ID("NMV", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Mario Party 3
    MATCH_ID("NMX", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK | FEAT_RPAK),                                            // Excitebike 64
    MATCH_ID("NNB", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK | FEAT_RPAK),                                            // Kobe Bryant in NBA Courtside
    MATCH_ID("NPP", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK),                                                        // Parlor! Pro 64: Pachinko Jikki Simulation Game
    MATCH_ID("NR7", SAVE_TYPE_EEPROM_16KBIT, FEAT_TPAK),                                                        // Robot Poncots 64: 7tsu no Umi no Caramel
    MATCH_ID("NRZ", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Ridge Racer 64
    MATCH_ID("NUB", SAVE_TYPE_EEPROM_16KBIT, FEAT_CPAK | FEAT_TPAK),                                            // PD Ultraman Battle Collection 64
    MATCH_ID("NYS", SAVE_TYPE_EEPROM_16KBIT, FEAT_RPAK),                                                        // Yoshi's Story

    // SRAM 256K
    MATCH_ID("CFZ", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK | FEAT_64DD_ENHANCED),                                    // F-Zero X (J)
    MATCH_ID("CPS", SAVE_TYPE_SRAM_256KBIT, FEAT_TPAK | FEAT_64DD_ENHANCED),                                    // Pocket Monsters Stadium (J)
    MATCH_ID("CZL", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK | FEAT_64DD_ENHANCED),                                    // Legend of Zelda: Ocarina of Time [Zelda no Densetsu - Toki no Ocarina (J)]
    MATCH_ID("NA2", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Virtual Pro Wrestling 2
    MATCH_ID("NAL", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Super Smash Bros. [Nintendo All-Star! Dairantou Smash Brothers (J)]
    MATCH_ID("NB5", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Biohazard 2 (J)
    MATCH_ID("NDD", SAVE_TYPE_SRAM_256KBIT, FEAT_EXP_PAK_REQUIRED | FEAT_64DD_CONVERSION),                      // 64DD Conversion Rom
    MATCH_ID("NFZ", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // F-Zero X (U + E)
    MATCH_ID("NG6", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Ganmare Goemon: Dero Dero Douchuu Obake Tenkomori
    MATCH_ID("NGP", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // Goemon: Mononoke Sugoroku
    MATCH_ID("NHY", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Hybrid Heaven (J)
    MATCH_ID("NIB", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Itoi Shigesato no Bass Tsuri No. 1 Kettei Ban!
    MATCH_ID("NJ5", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // Jikkyou Powerful Pro Yakyuu 5
    MATCH_ID("NJG", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Jinsei Game 64
    MATCH_ID("NKG", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Major League Baseball featuring Ken Griffey Jr.
    MATCH_ID("NMF", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK | FEAT_TPAK),                                             // Mario Golf 64
    MATCH_ID("NOB", SAVE_TYPE_SRAM_256KBIT, FEAT_NONE),                                                         // Ogre Battle 64: Person of Lordly Caliber
    MATCH_ID("NP4", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // Jikkyou Powerful Pro Yakyuu 4
    MATCH_ID("NP6", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_TPAK),                                             // Jikkyou Powerful Pro Yakyuu 6
    MATCH_ID("NPA", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_TPAK),                                             // Jikkyou Powerful Pro Yakyuu 2000
    MATCH_ID("NPE", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // Jikkyou Powerful Pro Yakyuu Basic Ban 2001
    MATCH_ID("NPM", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // Premier Manager 64
    MATCH_ID("NPS", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Jikkyou J.League 1999: Perfect Striker 2
    MATCH_ID("NRE", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Resident Evil 2
    MATCH_ID("NRI", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // New Tetris, The
    MATCH_ID("NS4", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_TPAK),                                             // Super Robot Taisen 64
    MATCH_ID("NSI", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // Fushigi no Dungeon: Fuurai no Shiren 2
    MATCH_ID("NT3", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK),                                                         // Shin Nihon Pro Wrestling - Toukon Road 2 - The Next Generation (J)
    MATCH_ID("NTE", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // 1080 Snowboarding
    MATCH_ID("NUM", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK | FEAT_TPAK),                                             // Nushi Zuri 64: Shiokaze ni Notte
    MATCH_ID("NUT", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_RPAK | FEAT_TPAK),                                 // Nushi Zuri 64
    MATCH_ID("NVB", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Bass Rush - ECOGEAR PowerWorm Championship (J)
    MATCH_ID("NVP", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_RPAK),                                             // Virtual Pro Wrestling 64
    MATCH_ID("NW2", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // WCW-nWo Revenge
    MATCH_ID("NWL", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Waialae Country Club: True Golf Classics
    MATCH_ID("NWX", SAVE_TYPE_SRAM_256KBIT, FEAT_CPAK | FEAT_RPAK),                                             // WWF WrestleMania 2000
    MATCH_ID("NYW", SAVE_TYPE_SRAM_256KBIT, FEAT_NONE),                                                         // Harvest Moon 64
    MATCH_ID("NZL", SAVE_TYPE_SRAM_256KBIT, FEAT_RPAK),                                                         // Legend of Zelda: Ocarina of Time (E)

    // SRAM 768K
    MATCH_ID("CDZ", SAVE_TYPE_SRAM_BANKED, FEAT_RPAK | FEAT_64DD_ENHANCED),                                     // Dezaemon 3D

    // FLASHRAM
    MATCH_ID("CP2", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_TPAK | FEAT_64DD_ENHANCED),                                  // Pocket Monsters Stadium 2 (J)
    MATCH_ID("NAF", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_CPAK | FEAT_RTC),                                            // Doubutsu no Mori
    MATCH_ID("NCC", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK),                                                       // Command & Conquer
    MATCH_ID("NCV", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_NONE),                                                       // Cubivore (Translation)
    MATCH_ID("NCK", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK),                                                       // NBA Courtside 2 featuring Kobe Bryant
    MATCH_ID("NDA", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_CPAK),                                                       // Derby Stallion 64
    MATCH_ID("NDP", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_EXP_PAK_REQUIRED),                                           // Dinosaur Planet (Unlicensed)
    MATCH_ID("NJF", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK),                                                       // Jet Force Gemini [Star Twins (J)]
    MATCH_ID("NKJ", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK),                                                       // Ken Griffey Jr.'s Slugfest
    MATCH_ID("NM6", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK),                                                       // Mega Man 64
    MATCH_ID("NMQ", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK),                                                       // Paper Mario
    MATCH_ID("NPF", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_NONE),                                                       // Pokemon Snap [Pocket Monsters Snap (J)]
    MATCH_ID("NPN", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_NONE),                                                       // Pokemon Puzzle League
    MATCH_ID("NPO", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_TPAK),                                                       // Pokemon Stadium
    MATCH_ID("NRH", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK),                                                       // Rockman Dash - Hagane no Boukenshin (J)
    MATCH_ID("NSQ", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK | FEAT_EXP_PAK_RECOMMENDED),                            // StarCraft 64
    MATCH_ID("NT9", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_NONE),                                                       // Tigger's Honey Hunt
    MATCH_ID("NW4", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_CPAK | FEAT_RPAK),                                           // WWF No Mercy
    MATCH_ID("NZS", SAVE_TYPE_FLASHRAM_1MBIT, FEAT_RPAK | FEAT_EXP_PAK_REQUIRED),                               // Legend of Zelda: Majora's Mask [Zelda no Densetsu - Mujura no Kamen (J)]

    MATCH_ID("NP3", SAVE_TYPE_FLASHRAM_PKST2, FEAT_TPAK),                                                       // Pokemon Stadium 2 [Pocket Monsters Stadium - Kin Gin (J)]

    // CONTROLLER PAK / NONE
    MATCH_ID("N22", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Ready 2 Rumble Boxing - Round 2
    MATCH_ID("N2M", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Madden Football 2002
    MATCH_ID("N2P", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Rampage 2 - Universal Tour
    MATCH_ID("N2V", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Chameleon Twist 2
    MATCH_ID("N32", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Army Men - Sarge's Heroes 2
    MATCH_ID("N3P", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Triple Play 2000
    MATCH_ID("N3T", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Tony Hawk's Pro Skater 3
    MATCH_ID("N4W", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // 40 Winks
    MATCH_ID("N64", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Kira to Kaiketsu! 64 Tanteidan
    MATCH_ID("N7I", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // FIFA Soccer 64 [FIFA 64 (E)]
    MATCH_ID("N8I", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // FIFA - Road to World Cup 98 [World Cup e no Michi (J)]
    MATCH_ID("N8M", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Madden Football 64
    MATCH_ID("N8W", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // World Cup '98
    MATCH_ID("N9B", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NBA Live '99
    MATCH_ID("N9C", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Nascar '99
    MATCH_ID("N9F", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // FIFA 99
    MATCH_ID("N9H", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NHL '99
    MATCH_ID("N9M", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Madden Football '99
    MATCH_ID("NAC", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Army Men - Air Combat
    MATCH_ID("NAH", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Asteroids Hyper 64
    MATCH_ID("NAI", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Midway's Greatest Arcade Hits Volume 1
    MATCH_ID("NAM", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Army Men - Sarge's Heroes
    MATCH_ID("NAR", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Armorines - Project S.W.A.R.M.
    MATCH_ID("NAS", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // All-Star Baseball 2001
    MATCH_ID("NAY", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Aidyn Chronicles - The First Mage
    MATCH_ID("NB2", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NBA In the Zone '99 [NBA Pro '99 (E)]
    MATCH_ID("NB3", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Bust-A-Move '99 [Bust-A-Move 3 DX (E)]
    MATCH_ID("NB4", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Bass Masters 2000
    MATCH_ID("NB8", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Beetle Adventure Racing (J)
    MATCH_ID("NB9", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // NBA Jam '99
    MATCH_ID("NBA", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NBA In the Zone '98 [NBA Pro '98 (E)]
    MATCH_ID("NBE", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // All-Star Baseball 2000
    MATCH_ID("NBF", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Bio F.R.E.A.K.S.
    MATCH_ID("NBI", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Blitz 2000
    MATCH_ID("NBJ", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Bakushou Jinsei 64 - Mezase! Resort Ou
    MATCH_ID("NBL", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Buck Bumble
    MATCH_ID("NBO", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Bottom of the 9th
    MATCH_ID("NBP", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Blues Brothers 2000
    MATCH_ID("NBQ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Battletanx - Global Assault
    MATCH_ID("NBR", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Milo's Astro Lanes
    MATCH_ID("NBS", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // All-Star Baseball '99
    MATCH_ID("NBU", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Bust-A-Move 2 - Arcade Edition
    MATCH_ID("NBW", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Super Bowling
    MATCH_ID("NBX", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Battletanx
    MATCH_ID("NBY", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Bug's Life, A
    MATCH_ID("NBZ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Blitz
    MATCH_ID("NCB", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Charlie Blast's Territory
    MATCH_ID("NCD", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Carmageddon 64
    MATCH_ID("NCE", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Nuclear Strike 64
    MATCH_ID("NCL", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // California Speed
    MATCH_ID("NCO", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Jeremy McGrath Supercross 2000
    MATCH_ID("NCS", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // S.C.A.R.S.
    MATCH_ID("NDC", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // SD Hiryuu no Ken Densetsu (J)
    MATCH_ID("NDE", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Destruction Derby 64
    MATCH_ID("NDF", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Dance Dance Revolution - Disney Dancing Museum
    MATCH_ID("NDH", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Duel Heroes
    MATCH_ID("NDM", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Doom 64
    MATCH_ID("NDN", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Duke Nukem 64
    MATCH_ID("NDQ", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Disney's Donald Duck - Goin' Quackers [Quack Attack (E)]
    MATCH_ID("NDS", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // J.League Dynamite Soccer 64
    MATCH_ID("NDT", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // South Park
    MATCH_ID("NDW", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Daikatana, John Romero's
    MATCH_ID("NDZ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Duke Nukem - Zero Hour
    MATCH_ID("NEG", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Extreme-G
    MATCH_ID("NET", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Quest 64 [Eltale Monsters (J) Holy Magic Century (E)]
    MATCH_ID("NF9", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Fox Sports College Hoops '99
    MATCH_ID("NFB", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Blitz 2001
    MATCH_ID("NFD", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Flying Dragon
    MATCH_ID("NFF", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Fighting Force 64
    MATCH_ID("NFL", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Madden Football 2001
    MATCH_ID("NFO", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Forsaken 64
    MATCH_ID("NFQ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Razor Freestyle Scooter
    MATCH_ID("NFR", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // F-1 Racing Championship
    MATCH_ID("NFS", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Famista 64
    MATCH_ID("NG2", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Extreme-G XG2
    MATCH_ID("NG5", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Ganbare Goemon - Neo Momoyama Bakufu no Odori [Mystical Ninja Starring Goemon]
    MATCH_ID("NGA", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Deadly Arts [G.A.S.P!! Fighter's NEXTream (E-J)]
    MATCH_ID("NGB", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Top Gear Hyper Bike
    MATCH_ID("NGD", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Gauntlet Legends (J)
    MATCH_ID("NGM", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Goemon's Great Adventure [Mystical Ninja 2 Starring Goemon]
    MATCH_ID("NGN", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Golden Nugget 64
    MATCH_ID("NGR", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Top Gear Rally (U)
    MATCH_ID("NGS", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Jikkyou G1 Stable
    MATCH_ID("NGX", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Gauntlet Legends
    MATCH_ID("NH5", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Nagano Winter Olympics '98 [Hyper Olympics in Nagano (J)]
    MATCH_ID("NH9", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NHL Breakaway '99
    MATCH_ID("NHC", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Hercules - The Legendary Journeys
    MATCH_ID("NHG", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // F-1 Pole Position 64
    MATCH_ID("NHG", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Human Grand Prix - New Generation
    MATCH_ID("NHK", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Hiryuu no Ken Twin
    MATCH_ID("NHL", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NHL Breakaway '98
    MATCH_ID("NHM", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Mia Hamm Soccer 64
    MATCH_ID("NHN", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Olympic Hockey Nagano '98
    MATCH_ID("NHO", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NHL Blades of Steel '99 [NHL Pro '99 (E)]
    MATCH_ID("NHS", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Hamster Monogatari 64
    MATCH_ID("NHT", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Hydro Thunder
    MATCH_ID("NHV", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Hybrid Heaven (U + E)
    MATCH_ID("NHW", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Hot Wheels Turbo Racing
    MATCH_ID("NHX", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Hexen
    MATCH_ID("NIS", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // International Superstar Soccer 2000
    MATCH_ID("NIV", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Space Invaders
    MATCH_ID("NJ2", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Wonder Project J2 - Koruro no Mori no Jozet (J)
    MATCH_ID("NJ3", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Jikkyou World Soccer 3
    MATCH_ID("NJA", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NBA Jam 2000
    MATCH_ID("NJE", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // J.League Eleven Beat 1997
    MATCH_ID("NJL", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // J.League Live 64
    MATCH_ID("NJP", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // International Superstar Soccer 64 [Jikkyo J-League Perfect Striker (J)]
    MATCH_ID("NJQ", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Batman Beyond - Return of the Joker [Batman of the Future - Return of the Joker (E)]
    MATCH_ID("NKE", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Knife Edge - Nose Gunner
    MATCH_ID("NKK", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Knockout Kings 2000
    MATCH_ID("NKM", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Pro Mahjong Kiwame 64 (J)
    MATCH_ID("NKR", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Rakuga Kids (E)
    MATCH_ID("NL2", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Top Gear Rally 2
    MATCH_ID("NLC", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Automobili Lamborghini [Super Speed Race 64 (J)]
    MATCH_ID("NLG", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // LEGO Racers
    MATCH_ID("NM3", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Monster Truck Madness 64
    MATCH_ID("NM4", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Mortal Kombat 4
    MATCH_ID("NM9", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Harukanaru Augusta Masters 98
    MATCH_ID("NMA", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Jangou Simulation Mahjong Do 64
    MATCH_ID("NMB", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Mike Piazza's Strike Zone
    MATCH_ID("NMD", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Madden Football 2000
    MATCH_ID("NMJ", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Mahjong 64
    MATCH_ID("NMM", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Mahjong Master
    MATCH_ID("NMT", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Magical Tetris Challenge
    MATCH_ID("NMY", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Mortal Kombat Mythologies - Sub-Zero
    MATCH_ID("NN2", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Nascar 2000
    MATCH_ID("NNC", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Nightmare Creatures
    MATCH_ID("NNL", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NBA Live 2000
    MATCH_ID("NNM", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Namco Museum 64
    MATCH_ID("NNR", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Pro Mahjong Tsuwamono 64 - Jansou Battle ni Chousen (J)
    MATCH_ID("NNS", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Beetle Adventure Racing
    MATCH_ID("NO7", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // The World Is Not Enough
    MATCH_ID("NOF", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Offroad Challenge
    MATCH_ID("NOH", SAVE_TYPE_NONE, FEAT_RPAK | FEAT_TPAK),                                                     // Transformers Beast Wars - Transmetals
    MATCH_ID("NOM", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Onegai Monsters
    MATCH_ID("NOW", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Brunswick Circuit Pro Bowling
    MATCH_ID("NP9", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Ms. Pac-Man - Maze Madness
    MATCH_ID("NPB", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Puzzle Bobble 64 (J)
    MATCH_ID("NPC", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Pachinko 365 Nichi (J)
    MATCH_ID("NPK", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Chou Kuukan Night Pro Yakyuu King (J)
    MATCH_ID("NPL", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Power League 64 (J)
    MATCH_ID("NPR", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // South Park Rally
    MATCH_ID("NPU", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Power Rangers - Lightspeed Rescue
    MATCH_ID("NPX", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Polaris SnoCross
    MATCH_ID("NPZ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Susume! Taisen Puzzle Dama Toukon! Marumata Chou (J)
    MATCH_ID("NQ2", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Quake 2
    MATCH_ID("NQ8", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Quarterback Club '98
    MATCH_ID("NQ9", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Quarterback Club '99
    MATCH_ID("NQB", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Quarterback Club 2000
    MATCH_ID("NQC", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Quarterback Club 2001
    MATCH_ID("NQK", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Quake 64
    MATCH_ID("NR2", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Rush 2 - Extreme Racing USA
    MATCH_ID("NR3", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Stunt Racer 64
    MATCH_ID("NR6", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Tom Clancy's Rainbow Six
    MATCH_ID("NRD", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Ready 2 Rumble Boxing
    MATCH_ID("NRG", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Rugrats - Scavenger Hunt [Treasure Hunt (E)]
    MATCH_ID("NRK", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Rugrats in Paris - The Movie
    MATCH_ID("NRO", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Road Rash 64
    MATCH_ID("NRP", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Rampage - World Tour
    MATCH_ID("NRR", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Roadster's Trophy
    MATCH_ID("NRT", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Rat Attack
    MATCH_ID("NRU", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // San Francisco Rush 2049
    MATCH_ID("NRV", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Re-Volt
    MATCH_ID("NRW", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Turok: Rage Wars
    MATCH_ID("NRX", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Robotron 64
    MATCH_ID("NS2", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Simcity 2000
    MATCH_ID("NSB", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Twisted Edge - Extreme Snowboarding [King Hill 64 - Extreme Snowboarding (J)]
    MATCH_ID("NSD", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Shadow Man
    MATCH_ID("NSF", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // San Francisco Rush - Extreme Racing
    MATCH_ID("NSG", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Shadowgate 64 - Trials Of The Four Towers
    MATCH_ID("NSH", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Saikyou Habu Shougi (J)
    MATCH_ID("NSJ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // J.League Tactics Soccer
    MATCH_ID("NSK", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Snowboard Kids [Snobow Kids (J)]
    MATCH_ID("NSL", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Spider-Man
    MATCH_ID("NSO", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // NBA Showtime - NBA on NBC
    MATCH_ID("NSP", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Superman
    MATCH_ID("NST", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Eikou no Saint Andrews
    MATCH_ID("NSX", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Supercross 2000
    MATCH_ID("NSY", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Scooby-Doo! - Classic Creep Capers
    MATCH_ID("NSZ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NFL Blitz - Special Edition
    MATCH_ID("NT2", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Turok 2 - Seeds of Evil [Violence Killer - Turok New Generation (J)]
    MATCH_ID("NT4", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // CyberTiger
    MATCH_ID("NTA", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Disney's Tarzan
    MATCH_ID("NTD", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // O.D.T. [Prototype]
    MATCH_ID("NTF", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Tony Hawk's Pro Skater
    MATCH_ID("NTH", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Toy Story 2 - Buzz Lightyear to the Rescue!
    MATCH_ID("NTI", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // WWF: Attitude
    MATCH_ID("NTK", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Turok 3 - Shadow of Oblivion
    MATCH_ID("NTO", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Shin Nihon Pro Wrestling - Toukon Road - Brave Spirits (J)
    MATCH_ID("NTQ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Tony Hawk's Pro Skater 2
    MATCH_ID("NTS", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Centre Court Tennis [Let's Smash (J)]
    MATCH_ID("NTT", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Tonic Trouble
    MATCH_ID("NTU", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Turok: Dinosaur Hunter [Turok: Jikuu Senshi (J)]
    MATCH_ID("NV3", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Micro Machines 64 Turbo
    MATCH_ID("NV8", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Vigilante 8
    MATCH_ID("NVC", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Virtual Chess 64
    MATCH_ID("NVG", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Vigilante 8 - Second Offense
    MATCH_ID("NVR", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Virtual Pool 64
    MATCH_ID("NW3", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // WCW: Nitro
    MATCH_ID("NW8", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Wayne Gretzky's 3D Hockey '98
    MATCH_ID("NWB", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Iggy's Reckin' Balls [Iggy-kun no Bura Bura Poyon (J)]
    MATCH_ID("NWD", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Winback - Covert Operations
    MATCH_ID("NWF", SAVE_TYPE_NONE, FEAT_RPAK),                                                                 // Wheel of Fortune
    MATCH_ID("NWG", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Wayne Gretzky's 3D Hockey
    MATCH_ID("NWI", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // ECW Hardcore Revolution
    MATCH_ID("NWK", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Michael Owens WLS 2000 [World League Soccer 2000 (E) / Telefoot Soccer 2000 (F)]
    MATCH_ID("NWM", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // WCW: Mayhem
    MATCH_ID("NWN", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // WCW vs. nWo - World Tour
    MATCH_ID("NWO", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // World Driver Championship
    MATCH_ID("NWP", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Wipeout 64
    MATCH_ID("NWS", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // International Superstar Soccer '98 [Jikkyo World Soccer - World Cup France '98 (J)]
    MATCH_ID("NWV", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // WCW: Backstage Assault
    MATCH_ID("NWW", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // WWF: War Zone
    MATCH_ID("NWZ", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // NBA In the Zone 2000
    MATCH_ID("NX2", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Gex 64 - Enter the Gecko
    MATCH_ID("NX3", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Gex 3 - Deep Cover Gecko
    MATCH_ID("NXF", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Xena Warrior Princess - The Talisman of Fate
    MATCH_ID("NXG", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // NBA Hangtime
    MATCH_ID("NY2", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Rayman 2 - The Great Escape
    MATCH_ID("NYP", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Paperboy
    MATCH_ID("NYW", SAVE_TYPE_NONE, FEAT_CPAK),                                                                 // Bokujou Monogatari 2
    MATCH_ID("NZO", SAVE_TYPE_NONE, FEAT_CPAK | FEAT_RPAK),                                                     // Battlezone - Rise of the Black Dogs

    MATCH_END,
};
// clang-format on


static void fix_rom_header_endianness (rom_header_t *rom_header, rom_info_t *rom_info) {
    uint8_t *raw = (uint8_t *) (rom_header);

    switch (rom_header->pi_dom1_config) {
        case PI_CONFIG_LITTLE_ENDIAN:
            rom_info->endianness = ENDIANNESS_LITTLE;
            for (int i = 0; i < sizeof(rom_header_t); i += 4) {
                SWAP_VARS(raw[i + 0], raw[i + 3]);
                SWAP_VARS(raw[i + 1], raw[i + 2]);
            }
            break;

        case PI_CONFIG_BYTE_SWAPPED:
            rom_info->endianness = ENDIANNESS_BYTE_SWAP;
            for (int i = 0; i < sizeof(rom_header_t); i += 2) {
                SWAP_VARS(raw[i + 0], raw[i + 1]);
            }
            break;

        default:
            rom_info->endianness = ENDIANNESS_BIG;
            break;
    }
}

static bool compare_id (const match_t *match, rom_header_t *rom_header) {
    int characters_to_check = (match->type == MATCH_TYPE_ID) ? 3 : 4;

    for (int i = (characters_to_check - 1); i >= 0; i--) {
        if (match->fields.id[i] != rom_header->game_code[i]) {
            return false;
        }
    }

    if (match->type == MATCH_TYPE_ID_REGION_VERSION) {
        if (match->fields.version != rom_header->version) {
            return false;
        }
    }

    return true;
}

static match_t find_rom_in_database (rom_header_t *rom_header) {
    const match_t *match;

    for (match = database; match->type != MATCH_TYPE_END; match++) {
        switch (match->type) {
            case MATCH_TYPE_ID:
            case MATCH_TYPE_ID_REGION:
            case MATCH_TYPE_ID_REGION_VERSION:
                if (compare_id(match, rom_header)) {
                    return *match;
                }
                break;

            case MATCH_TYPE_CHECK_CODE:
                if (match->fields.check_code == rom_header->check_code) {
                    return *match;
                }
                break;

            case MATCH_TYPE_HOMEBREW_HEADER:
                if (strncmp(match->fields.id, rom_header->unique_code, sizeof(rom_header->unique_code)) == 0) {
                    return *match;
                }
                break;

            default:
                break;
        }
    }

    return *match;
}

static rom_cic_type_t detect_cic_type (uint8_t *ipl3) {
    switch (cic_detect(ipl3)) {
        case CIC_5101: return ROM_CIC_TYPE_5101;
        case CIC_5167: return ROM_CIC_TYPE_5167;
        case CIC_6101: return ROM_CIC_TYPE_6101;
        case CIC_7102: return ROM_CIC_TYPE_7102;
        case CIC_x102: return ROM_CIC_TYPE_x102;
        case CIC_x103: return ROM_CIC_TYPE_x103;
        case CIC_x105: return ROM_CIC_TYPE_x105;
        case CIC_x106: return ROM_CIC_TYPE_x106;
        case CIC_8301: return ROM_CIC_TYPE_8301;
        case CIC_8302: return ROM_CIC_TYPE_8302;
        case CIC_8303: return ROM_CIC_TYPE_8303;
        case CIC_8401: return ROM_CIC_TYPE_8401;
        case CIC_8501: return ROM_CIC_TYPE_8501;
        default: return ROM_CIC_TYPE_UNKNOWN;
    }
}

static uint32_t fix_boot_address (rom_cic_type_t cic_type, uint32_t boot_address) {
    switch (cic_type) {
        case ROM_CIC_TYPE_x103: return (boot_address - 0x100000);
        case ROM_CIC_TYPE_x106: return (boot_address - 0x200000);
        default: return boot_address;
    }
}

static rom_tv_type_t determine_tv_type (rom_destination_type_t rom_destination_code) {
        // check the market type from the ROM destination_code and return best guess!
        switch (rom_destination_code) {
            case MARKET_NORTH_AMERICA:
            case MARKET_CANADIAN:
            case MARKET_CHINESE: // (China is a PAL region, but was never used. The Chinese iQue patches seem use this would be NTSC)
            case MARKET_KOREAN:
            case MARKET_JAPANESE:
            case MARKET_JAPANESE_MULTI:
            case MARKET_GATEWAY64_NTSC:
                return ROM_TV_TYPE_NTSC;
            case MARKET_BRAZILIAN:
                return ROM_TV_TYPE_MPAL;
            case MARKET_AUSTRALIAN:
            case MARKET_DUTCH:
            case MARKET_FRENCH:
            case MARKET_GERMAN:
            case MARKET_ITALIAN:
            case MARKET_SCANDINAVIAN:
            case MARKET_SPANISH:
            case MARKET_GATEWAY64_PAL:
            case MARKET_EUROPEAN_BASIC:
            // FIXME: There might be some interesting errors with OTHER_X and OTHER_Y (e.g. TGR Asia).
            // But they are mainly PAL regions.
            case MARKET_OTHER_X:
            case MARKET_OTHER_Y:
                return ROM_TV_TYPE_PAL;
            // FIXME: We cannot be sure on these markets, so just return the default for the moment!
            case MARKET_OTHER_Z:
            default:
                return ROM_TV_TYPE_UNKNOWN;
        }
}

static void extract_rom_info (match_t *match, rom_header_t *rom_header, rom_info_t *rom_info) {
    rom_info->cic_type = detect_cic_type(rom_header->ipl3);

    if (match->type == MATCH_TYPE_HOMEBREW_HEADER) {
        if (rom_header->version & (1 << 0)) {
            match->data.feat |= FEAT_RTC;
        }
        switch ((rom_header->version & 0xF0) >> 4) {
            case 0: match->data.save = SAVE_TYPE_NONE; break;
            case 1: match->data.save = SAVE_TYPE_EEPROM_4KBIT; break;
            case 2: match->data.save = SAVE_TYPE_EEPROM_16KBIT; break;
            case 3: match->data.save = SAVE_TYPE_SRAM_256KBIT; break;
            case 4: match->data.save = SAVE_TYPE_SRAM_BANKED; break;
            case 5: match->data.save = SAVE_TYPE_FLASHRAM_1MBIT; break;
            case 6: match->data.save = SAVE_TYPE_SRAM_1MBIT; break;
            default: match->data.save = SAVE_TYPE_NONE; break;
        }
    }

    rom_info->clock_rate = (rom_header->clock_rate == CLOCK_RATE_DEFAULT) ? 62.5f : (rom_header->clock_rate & ~(CLOCK_RATE_DEFAULT)) / 1000000.0f;
    rom_info->boot_address = fix_boot_address(rom_info->cic_type, rom_header->boot_address);
    rom_info->libultra.version = rom_header->libultra.version;
    rom_info->libultra.revision = rom_header->libultra.revision;
    rom_info->check_code = rom_header->check_code;
    memcpy(rom_info->title, rom_header->title, sizeof(rom_info->title));
    memcpy(rom_info->game_code, rom_header->game_code, sizeof(rom_info->game_code));
    rom_info->version = rom_header->version;

    rom_info->save_type = match->data.save;
    rom_info->tv_type = determine_tv_type(rom_info->destination_code);

    rom_info->features.controller_pak = (match->data.feat & FEAT_CPAK);
    rom_info->features.rumble_pak = (match->data.feat & FEAT_RPAK);
    rom_info->features.transfer_pak = (match->data.feat & FEAT_TPAK);
    rom_info->features.voice_recognition_unit = (match->data.feat & FEAT_VRU);
    rom_info->features.real_time_clock = (match->data.feat & FEAT_RTC);
    rom_info->features.disk_conversion = (match->data.feat & FEAT_64DD_CONVERSION);
    rom_info->features.combo_rom_disk_game = (match->data.feat & FEAT_64DD_ENHANCED);
    if (match->data.feat & FEAT_EXP_PAK_REQUIRED) {
        rom_info->features.expansion_pak = EXPANSION_PAK_REQUIRED;
    } else if (match->data.feat & FEAT_EXP_PAK_RECOMMENDED) {
        rom_info->features.expansion_pak = EXPANSION_PAK_RECOMMENDED;
    } else if (match->data.feat & FEAT_EXP_PAK_UNUSED) {
        rom_info->features.expansion_pak = EXPANSION_PAK_UNUSED;
    } else if (match->data.feat & FEAT_EXP_PAK_BROKEN) {
        rom_info->features.expansion_pak = EXPANSION_PAK_FAULTY;
    } else {
        rom_info->features.expansion_pak = EXPANSION_PAK_UNSPECIFIED;
    }

    rom_info->meta.name = strdup("");
    rom_info->meta.author = strdup("Not specified");
    rom_info->meta.release_date = strdup("Not specified");
    rom_info->meta.osi_license = strdup("Not specified");
    rom_info->meta.website = strdup("Not specified");
    rom_info->meta.age_rating = 0;
    rom_info->meta.num_players = 1;
    rom_info->meta.short_description = strdup("");
    rom_info->meta.size_limit_exceeded = false;

    rom_info->settings.cheats_enabled = false;
    rom_info->settings.patches_enabled = false;
    rom_info->settings.clear_rdram_enabled = false;
}

/**
 * `@brief` Safely replace a heap-allocated string.
 *
 * Duplicates `@p` src first. Only if allocation succeeds does it free the
 * existing *dst and replace it. Returns false on OOM; *dst is unchanged.
 */
static bool replace_owned_string(char **dst, const char *src) {
    char *copy = strdup(src ? src : "");
    if (!copy) {
        return false;
    }
    free(*dst);
    *dst = copy;
    return true;
}

/**
 * @brief Try to load metadata from a ZIP file path (e.g., .meta file)
 * 
 * Opens a ZIP file and extracts metadata.ini from it, parsing the content
 * directly from memory without writing to disk.
 * 
 * @param zip_path Path to ZIP file
 * @param rom_info Output: metadata loaded into rom_info->meta
 * @return true if metadata was found and loaded, false otherwise
 */
static bool load_metadata_from_zip_file (const char *zip_path, rom_info_t *rom_info) {
    debugf("[META] load_metadata_from_zip_file: path='%s'\n", zip_path);
    
    if (!zip_path || !rom_info) {
        debugf("[META] load_metadata_from_zip_file: invalid args\n");
        return false;
    }
    
    mz_zip_archive zip = {0};
    if (!mz_zip_reader_init_file(&zip, zip_path, 0)) {
        debugf("[META] load_metadata_from_zip_file: mz_zip_reader_init_file failed\n");
        return false;
    }
    debugf("[META] load_metadata_from_zip_file: ZIP opened successfully\n");
    
    // Look for metadata.ini entry
    mz_uint file_index = mz_zip_reader_locate_file(&zip, "metadata.ini", NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    if (file_index == MZ_UINT32_MAX) {
        debugf("[META] load_metadata_from_zip_file: metadata.ini not found in ZIP\n");
        mz_zip_reader_end(&zip);
        return false;
    }
    debugf("[META] load_metadata_from_zip_file: metadata.ini found (index=%lu)\n", (unsigned long)file_index);
    
    // Get file stats
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat)) {
        debugf("[META] load_metadata_from_zip_file: mz_zip_reader_file_stat failed\n");
        mz_zip_reader_end(&zip);
        return false;
    }
    
    // Extract to memory
    if (file_stat.m_uncomp_size > (uint64_t)(SIZE_MAX - 1)) {
        debugf("[META] load_metadata_from_zip_file: metadata.ini too large (%llu)\n", (unsigned long long)file_stat.m_uncomp_size);
        mz_zip_reader_end(&zip);
        return false;
    }

    if (file_stat.m_uncomp_size > ROM_METADATA_INI_MAX_SIZE) {
        debugf("[META] load_metadata_from_zip_file: metadata.ini exceeds cap (%llu > %u)\n",
            (unsigned long long)file_stat.m_uncomp_size,
            ROM_METADATA_INI_MAX_SIZE);
        rom_info->meta.size_limit_exceeded = true;
        mz_zip_reader_end(&zip);
        return false;
    }

    size_t uncomp_size = (size_t)file_stat.m_uncomp_size;
    debugf("[META] load_metadata_from_zip_file: compressed=%llu, uncompressed=%zu\n", (unsigned long long)file_stat.m_comp_size, uncomp_size);
    char *metadata_content = scratch_malloc(uncomp_size + 1);
    bool used_scratch = true;
    if (!metadata_content) {
        used_scratch = false;
        metadata_content = malloc(uncomp_size + 1);
    }
    if (!metadata_content) {
        debugf("[META] load_metadata_from_zip_file: allocation failed for %zu bytes\n", uncomp_size + 1);
        mz_zip_reader_end(&zip);
        return false;
    }
    
    if (!mz_zip_reader_extract_to_mem(&zip, file_index, metadata_content, uncomp_size, 0)) {
        debugf("[META] load_metadata_from_zip_file: mz_zip_reader_extract_to_mem failed\n");
        if (used_scratch) {
            scratch_free(metadata_content);
        } else {
            free(metadata_content);
        }
        mz_zip_reader_end(&zip);
        return false;
    }
    debugf("[META] load_metadata_from_zip_file: extracted %zu bytes\n", uncomp_size);
    
    metadata_content[uncomp_size] = '\0';
    mz_zip_reader_end(&zip);
    
    // Parse from buffer using ini parser (no disk I/O)
    ini_t *meta_ini = ini_parse_buffer(metadata_content, uncomp_size);
    if (used_scratch) {
        scratch_free(metadata_content);
    } else {
        free(metadata_content);
    }
    
    bool success = false;
    if (meta_ini) {
        bool ok = true;
        ok &= replace_owned_string(&rom_info->meta.metadata_zip_path, zip_path);
        ok &= replace_owned_string(&rom_info->meta.name,              ini_get_string(meta_ini, "meta", "name",         ""));
        ok &= replace_owned_string(&rom_info->meta.author,            ini_get_string(meta_ini, "meta", "author",       "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.release_date,      ini_get_string(meta_ini, "meta", "release-date", "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.osi_license,       ini_get_string(meta_ini, "meta", "osi-license",  "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.website,           ini_get_string(meta_ini, "meta", "website",      "Not specified"));
        rom_info->meta.age_rating = ini_get_int(meta_ini, "meta", "age-rating", 0);
        rom_info->meta.num_players = ini_get_int(meta_ini, "meta", "num-players", 1);
        ok &= replace_owned_string(&rom_info->meta.short_description, ini_get_string(meta_ini, "meta", "short-desc",   ""));
        static const char *boxart_keys[] = { "front", "back", "top", "bottom", "left", "right" };
        for (size_t i = 0; i < 6; i++) {
            ok &= replace_owned_string(&rom_info->meta.boxart[i], ini_get_string(meta_ini, "boxart", boxart_keys[i], ""));
        }
        static const char *cartart_keys[] = { "front", "back" };
        for (size_t i = 0; i < 2; i++) {
            ok &= replace_owned_string(&rom_info->meta.cartart[i], ini_get_string(meta_ini, "cartart", cartart_keys[i], ""));
        }
        for (size_t i = 0; i < rom_info->meta.screenshot_count; i++) {
            free(rom_info->meta.screenshots[i]);
            rom_info->meta.screenshots[i] = NULL;
        }
        rom_info->meta.screenshot_count = 0;
        const char *screenshots = ini_get_string(meta_ini, "meta", "screenshots", "");
        char *list = strdup(screenshots);
        if (list != NULL) {
            char *save = NULL;
            for (char *filename = strtok_r(list, ",", &save);
                 filename != NULL && rom_info->meta.screenshot_count < ROM_METADATA_MAX_SCREENSHOTS;
                 filename = strtok_r(NULL, ",", &save)) {
                while (*filename == ' ' || *filename == '\t') filename++;
                char *end = filename + strlen(filename);
                while (end > filename && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
                if (*filename != '\0') {
                    rom_info->meta.screenshots[rom_info->meta.screenshot_count] = strdup(filename);
                    if (rom_info->meta.screenshots[rom_info->meta.screenshot_count] != NULL) {
                        rom_info->meta.screenshot_count++;
                    }
                }
            }
            free(list);
        }
        ini_free(meta_ini);
        success = ok;
        if (ok) {
            debugf("[META] Loaded from ZIP: name='%s', author='%s'\n", rom_info->meta.name, rom_info->meta.author);
        } else {
            debugf("[META] load_metadata_from_zip_file: one or more strdup failed (OOM)\n");
        }
    }
    debugf("[META] load_metadata_from_zip_file: returning %d\n", success);
    return success;
}

/**
 * @brief Try to load metadata from embedded ZIP in ROM file
 * 
 * If the ROM has the metadata flag set (header byte 0x38 bit 0),
 * this function extracts metadata.ini from the embedded ZIP appended
 * to the ROM and parses it.
 * 
 * @param rom_path Path to the ROM file
 * @param rom_header ROM header to check for metadata flag
 * @param rom_info Output: metadata loaded into rom_info->meta
 * @return true if embedded metadata was found and loaded, false otherwise
 */
static bool load_rom_meta_from_embedded_zip (const char *rom_path, rom_header_t *rom_header, rom_info_t *rom_info) {
    debugf("[META] load_rom_meta_from_embedded_zip: path='%s'\n", rom_path);
    
    // Check if ROM has embedded metadata flag (byte 0x38 bit 0)
    uint8_t *raw = (uint8_t *)rom_header;
    uint8_t flag_byte = raw[0x38];
    debugf("[META] load_rom_meta_from_embedded_zip: header[0x38]=0x%02x, flag=%d\n", flag_byte, (flag_byte & 1));
    
    if ((flag_byte & 1) == 0) {
        // No embedded metadata flag
        debugf("[META] load_rom_meta_from_embedded_zip: no metadata flag set\n");
        return false;
    }
    
    FILE *rom_file = fopen(rom_path, "rb");
    if (!rom_file) {
        debugf("[META] load_rom_meta_from_embedded_zip: failed to open ROM file\n");
        return false;
    }
    
    // Get file size
    fseek(rom_file, 0, SEEK_END);
    long file_size = ftell(rom_file);
    rewind(rom_file);
    debugf("[META] load_rom_meta_from_embedded_zip: ROM file size=%ld bytes\n", file_size);
    
    if (file_size < 100) {
        debugf("[META] load_rom_meta_from_embedded_zip: file too small\n");
        fclose(rom_file);
        return false;
    }
    
    mz_zip_archive zip = {0};
    if (!mz_zip_reader_init_file(&zip, rom_path, 0)) {
        debugf("[META] load_rom_meta_from_embedded_zip: mz_zip_reader_init_file failed\n");
        fclose(rom_file);
        return false;
    }
    debugf("[META] load_rom_meta_from_embedded_zip: ZIP initialized\n");
    
    // Look for metadata.ini entry
    mz_uint file_index = mz_zip_reader_locate_file(&zip, "metadata.ini", NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    if (file_index == MZ_UINT32_MAX) {
        debugf("[META] load_rom_meta_from_embedded_zip: metadata.ini not found in embedded ZIP\n");
        mz_zip_reader_end(&zip);
        fclose(rom_file);
        return false;
    }
    debugf("[META] load_rom_meta_from_embedded_zip: metadata.ini found (index=%lu)\n", (unsigned long)file_index);
    
    // Extract metadata.ini to memory
    mz_zip_archive_file_stat file_stat;
    if (!mz_zip_reader_file_stat(&zip, file_index, &file_stat)) {
        debugf("[META] load_rom_meta_from_embedded_zip: mz_zip_reader_file_stat failed\n");
        mz_zip_reader_end(&zip);
        fclose(rom_file);
        return false;
    }
    
    if (file_stat.m_uncomp_size > (uint64_t)(SIZE_MAX - 1)) {
        debugf("[META] load_rom_meta_from_embedded_zip: metadata.ini too large (%llu)\n", (unsigned long long)file_stat.m_uncomp_size);
        mz_zip_reader_end(&zip);
        fclose(rom_file);
        return false;
    }

    if (file_stat.m_uncomp_size > ROM_METADATA_INI_MAX_SIZE) {
        debugf("[META] load_rom_meta_from_embedded_zip: metadata.ini exceeds cap (%llu > %u)\n",
            (unsigned long long)file_stat.m_uncomp_size,
            ROM_METADATA_INI_MAX_SIZE);
        rom_info->meta.size_limit_exceeded = true;
        mz_zip_reader_end(&zip);
        fclose(rom_file);
        return false;
    }

    size_t uncomp_size = (size_t)file_stat.m_uncomp_size;
    debugf("[META] load_rom_meta_from_embedded_zip: size=%zu (compressed=%llu)\n", uncomp_size, (unsigned long long)file_stat.m_comp_size);
    char *metadata_content = scratch_malloc(uncomp_size + 1);
    bool used_scratch = true;
    if (!metadata_content) {
        used_scratch = false;
        metadata_content = malloc(uncomp_size + 1);
    }
    if (!metadata_content) {
        debugf("[META] load_rom_meta_from_embedded_zip: allocation failed\n");
        mz_zip_reader_end(&zip);
        fclose(rom_file);
        return false;
    }
    
    if (!mz_zip_reader_extract_to_mem(&zip, file_index, metadata_content, uncomp_size, 0)) {
        debugf("[META] load_rom_meta_from_embedded_zip: mz_zip_reader_extract_to_mem failed\n");
        if (used_scratch) {
            scratch_free(metadata_content);
        } else {
            free(metadata_content);
        }
        mz_zip_reader_end(&zip);
        fclose(rom_file);
        return false;
    }
    debugf("[META] load_rom_meta_from_embedded_zip: extracted successfully\n");
    
    metadata_content[uncomp_size] = '\0';
    mz_zip_reader_end(&zip);
    fclose(rom_file);
    
    // Parse from buffer using ini parser (no disk I/O needed)
    ini_t *meta_ini = ini_parse_buffer(metadata_content, uncomp_size);
    if (used_scratch) {
        scratch_free(metadata_content);
    } else {
        free(metadata_content);
    }
    
    bool success = false;
    if (meta_ini) {
        bool ok = true;
        ok &= replace_owned_string(&rom_info->meta.metadata_zip_path, rom_path);
        ok &= replace_owned_string(&rom_info->meta.name,              ini_get_string(meta_ini, "meta", "name",         ""));
        ok &= replace_owned_string(&rom_info->meta.author,            ini_get_string(meta_ini, "meta", "author",       "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.release_date,      ini_get_string(meta_ini, "meta", "release-date", "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.osi_license,       ini_get_string(meta_ini, "meta", "osi-license",  "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.website,           ini_get_string(meta_ini, "meta", "website",      "Not specified"));
        rom_info->meta.age_rating = ini_get_int(meta_ini, "meta", "age-rating", 0);
        rom_info->meta.num_players = ini_get_int(meta_ini, "meta", "num-players", 1);
        ok &= replace_owned_string(&rom_info->meta.short_description, ini_get_string(meta_ini, "meta", "short-desc",   ""));
        ok &= replace_owned_string(&rom_info->meta.long_description_fn, ini_get_string(meta_ini, "meta", "long-desc-fn", ""));
        static const char *boxart_keys[] = { "front", "back", "top", "bottom", "left", "right" };
        for (size_t i = 0; i < 6; i++) {
            ok &= replace_owned_string(&rom_info->meta.boxart[i], ini_get_string(meta_ini, "boxart", boxart_keys[i], ""));
        }
        static const char *cartart_keys[] = { "front", "back" };
        for (size_t i = 0; i < 2; i++) {
            ok &= replace_owned_string(&rom_info->meta.cartart[i], ini_get_string(meta_ini, "cartart", cartart_keys[i], ""));
        }
        for (size_t i = 0; i < rom_info->meta.screenshot_count; i++) {
            free(rom_info->meta.screenshots[i]);
            rom_info->meta.screenshots[i] = NULL;
        }
        rom_info->meta.screenshot_count = 0;
        const char *screenshots = ini_get_string(meta_ini, "meta", "screenshots", "");
        char *list = strdup(screenshots);
        if (list != NULL) {
            char *save = NULL;
            for (char *filename = strtok_r(list, ",", &save);
                 filename != NULL && rom_info->meta.screenshot_count < ROM_METADATA_MAX_SCREENSHOTS;
                 filename = strtok_r(NULL, ",", &save)) {
                while (*filename == ' ' || *filename == '\t') filename++;
                char *end = filename + strlen(filename);
                while (end > filename && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
                if (*filename != '\0') {
                    rom_info->meta.screenshots[rom_info->meta.screenshot_count] = strdup(filename);
                    if (rom_info->meta.screenshots[rom_info->meta.screenshot_count] != NULL) {
                        rom_info->meta.screenshot_count++;
                    }
                }
            }
            free(list);
        }
        ini_free(meta_ini);
        success = ok;
        if (ok) {
            debugf("[META] Loaded from ZIP: name='%s', author='%s'\n", rom_info->meta.name, rom_info->meta.author);
        } else {
            debugf("[META] load_metadata_from_zip_file: one or more strdup failed (OOM)\n");
        }
    }
    
    debugf("[META] load_rom_meta_from_embedded_zip: returning %d\n", success);
    return success;
}

static void load_rom_meta_from_file (path_t *path, rom_info_t *rom_info) {
    path_t *rom_info_meta_path = path_clone(path);

    path_ext_replace(rom_info_meta_path, "meta");

    const char *meta_path_str = path_get(rom_info_meta_path);
    debugf("[META] load_rom_meta_from_file: looking for '%s'\n", meta_path_str);
    
    // Try to load as ZIP file first (handles .meta files that are ZIP archives)
    if (load_metadata_from_zip_file(meta_path_str, rom_info)) {
        debugf("[META] load_rom_meta_from_file: loaded as ZIP file\n");
        path_free(rom_info_meta_path);
        return;
    }
    
    // Fall back to flat INI format
    debugf("[META] load_rom_meta_from_file: trying flat INI format\n");
    // check for ROM metadata INI file by replacing the current path's ROM extension with metadata.ini
    path_ext_replace(rom_info_meta_path, "metadata.ini");
    meta_path_str = path_get(rom_info_meta_path);

    if (!file_exists(path_get(rom_info_meta_path))) {
        debugf("[META] load_rom_meta_from_file: metadata.ini not found at '%s'\n", meta_path_str);
        // If that file does not exist, fall back to metadata database using game_code (like boxart uses).
        // TODO: we should probably check the homebrew path as well.
        char gamecode_str[8];
        path_t *fallback_meta_path= path_init("sd:/", "menu/metadata"); // should be menu->storage_prefix and METADATA_BASE_DIRECTORY
        // FIXME: should use METADATA_BASE_DIRECTORY and path functions, but this is simpler for now since we just want to check for existence of the file.
        snprintf(
            gamecode_str,
            sizeof(gamecode_str),
            "%c/%c/%c/%c",
            rom_info->game_code[0], rom_info->game_code[1], rom_info->game_code[2], rom_info->game_code[3]
        );
        path_push(fallback_meta_path, gamecode_str);

        path_push(fallback_meta_path, "metadata.ini");
        debugf("[META] load_rom_meta_from_file: trying fallback path '%s'\n", path_get(fallback_meta_path));
        path_free(rom_info_meta_path);
        rom_info_meta_path = fallback_meta_path;
        meta_path_str = path_get(rom_info_meta_path);
    }
    
    debugf("[META] load_rom_meta_from_file: using metadata.ini at '%s'\n", meta_path_str);
    ini_t *rom_meta_ini = ini_load(meta_path_str);
    if (rom_meta_ini) {
        debugf("[META] load_rom_meta_from_file: loaded as INI file\n");
        bool ok = true;
        ok &= replace_owned_string(&rom_info->meta.name,              ini_get_string(rom_meta_ini, "meta", "name",         ""));
        ok &= replace_owned_string(&rom_info->meta.author,            ini_get_string(rom_meta_ini, "meta", "author",       "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.release_date,      ini_get_string(rom_meta_ini, "meta", "release-date", "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.osi_license,       ini_get_string(rom_meta_ini, "meta", "osi-license",  "Not specified"));
        ok &= replace_owned_string(&rom_info->meta.website,           ini_get_string(rom_meta_ini, "meta", "website",      "Not specified"));
        rom_info->meta.age_rating = ini_get_int(rom_meta_ini, "meta", "age-rating", 0);
        rom_info->meta.num_players = ini_get_int(rom_meta_ini, "meta", "num-players", 1);
        ok &= replace_owned_string(&rom_info->meta.short_description, ini_get_string(rom_meta_ini, "meta", "short-desc",   ""));
        ok &= replace_owned_string(&rom_info->meta.long_description_fn, ini_get_string(rom_meta_ini, "meta", "long-desc-fn", ""));
        static const char *boxart_keys[] = { "front", "back", "top", "bottom", "left", "right" };
        for (size_t i = 0; i < 6; i++) {
            ok &= replace_owned_string(&rom_info->meta.boxart[i], ini_get_string(rom_meta_ini, "boxart", boxart_keys[i], ""));
        }
        static const char *cartart_keys[] = { "front", "back" };
        for (size_t i = 0; i < 2; i++) {
            ok &= replace_owned_string(&rom_info->meta.cartart[i], ini_get_string(rom_meta_ini, "cartart", cartart_keys[i], ""));
        }
        for (size_t i = 0; i < rom_info->meta.screenshot_count; i++) {
            free(rom_info->meta.screenshots[i]);
            rom_info->meta.screenshots[i] = NULL;
        }
        rom_info->meta.screenshot_count = 0;
        const char *screenshots = ini_get_string(rom_meta_ini, "meta", "screenshots", "");
        char *list = strdup(screenshots);
        if (list != NULL) {
            char *save = NULL;
            for (char *filename = strtok_r(list, ",", &save);
                 filename != NULL && rom_info->meta.screenshot_count < ROM_METADATA_MAX_SCREENSHOTS;
                 filename = strtok_r(NULL, ",", &save)) {
                while (*filename == ' ' || *filename == '\t') filename++;
                char *end = filename + strlen(filename);
                while (end > filename && (end[-1] == ' ' || end[-1] == '\t')) *--end = '\0';
                if (*filename != '\0') {
                    rom_info->meta.screenshots[rom_info->meta.screenshot_count] = strdup(filename);
                    if (rom_info->meta.screenshots[rom_info->meta.screenshot_count] != NULL) {
                        rom_info->meta.screenshot_count++;
                    }
                }
            }
            free(list);
        }
        ini_free(rom_meta_ini);
        if (ok) {
            debugf("[META] Loaded from INI file: name='%s', author='%s'\n", rom_info->meta.name, rom_info->meta.author);
        } else {
            debugf("[META] load_rom_meta_from_file: one or more strdup failed (OOM)\n");
        }
    }
    debugf("[META] load_rom_meta_from_file: complete\n");
    path_free(rom_info_meta_path);
}

bool rom_info_extract_metadata_image(const rom_info_t *rom_info, const char *filename,
                                     uint8_t **data, size_t *size) {
    if (!rom_info || !rom_info->meta.metadata_zip_path || !filename || !data || !size ||
        filename[0] == '/' || strstr(filename, "..") != NULL) {
        return false;
    }

    *data = NULL;
    *size = 0;
    mz_zip_archive zip = {0};
    if (!mz_zip_reader_init_file(&zip, rom_info->meta.metadata_zip_path, 0)) return false;

    mz_uint index = mz_zip_reader_locate_file(&zip, filename, NULL, MZ_ZIP_FLAG_CASE_SENSITIVE);
    mz_zip_archive_file_stat stat;
    bool ok = index != MZ_UINT32_MAX && mz_zip_reader_file_stat(&zip, index, &stat) &&
              stat.m_uncomp_size > 0 && stat.m_uncomp_size <= 2 * 1024 * 1024;
    if (ok) {
        uint8_t *buffer = malloc((size_t) stat.m_uncomp_size);
        ok = buffer != NULL && mz_zip_reader_extract_to_mem(&zip, index, buffer, (size_t) stat.m_uncomp_size, 0);
        if (ok) {
            *data = buffer;
            *size = (size_t) stat.m_uncomp_size;
        } else {
            free(buffer);
        }
    }
    mz_zip_reader_end(&zip);
    return ok;
}

void rom_info_free_meta(rom_info_t *rom_info) {
    if (!rom_info) {
        return;
    }
    
    if (rom_info->meta.name) {
        free(rom_info->meta.name);
        rom_info->meta.name = NULL;
    }
    if (rom_info->meta.author) {
        free(rom_info->meta.author);
        rom_info->meta.author = NULL;
    }
    if (rom_info->meta.release_date) {
        free(rom_info->meta.release_date);
        rom_info->meta.release_date = NULL;
    }
    if (rom_info->meta.osi_license) {
        free(rom_info->meta.osi_license);
        rom_info->meta.osi_license = NULL;
    }
    if (rom_info->meta.website) {
        free(rom_info->meta.website);
        rom_info->meta.website = NULL;
    }
    if (rom_info->meta.short_description) {
        free(rom_info->meta.short_description);
        rom_info->meta.short_description = NULL;
    }
    if (rom_info->meta.long_description_fn) {
        free(rom_info->meta.long_description_fn);
        rom_info->meta.long_description_fn = NULL;
    }
    free(rom_info->meta.metadata_zip_path);
    rom_info->meta.metadata_zip_path = NULL;
    for (size_t i = 0; i < 6; i++) {
        free(rom_info->meta.boxart[i]);
        rom_info->meta.boxart[i] = NULL;
    }
    for (size_t i = 0; i < 2; i++) {
        free(rom_info->meta.cartart[i]);
        rom_info->meta.cartart[i] = NULL;
    }
    for (size_t i = 0; i < rom_info->meta.screenshot_count; i++) {
        free(rom_info->meta.screenshots[i]);
        rom_info->meta.screenshots[i] = NULL;
    }
    rom_info->meta.screenshot_count = 0;
}

static void load_rom_config_from_file (path_t *path, rom_info_t *rom_info) {
    path_t *rom_info_path = path_clone(path);

    path_ext_replace(rom_info_path, "ini");

    ini_t *rom_config_ini = ini_load(path_get(rom_info_path));

    rom_info->boot_override.cic = false;
    rom_info->boot_override.save = false;
    rom_info->boot_override.tv = false;

    if (rom_config_ini) {
        // general
        rom_info->settings.cheats_enabled = ini_get_bool(rom_config_ini, "", "cheats_enabled", false);
        rom_info->settings.patches_enabled = ini_get_bool(rom_config_ini, "", "patches_enabled", false);
        rom_info->settings.clear_rdram_enabled = ini_get_bool(rom_config_ini, "", "clear_rdram_enabled", false);
        
        // overrides
        rom_info->boot_override.cic_type = ini_get_int(rom_config_ini, "custom_boot", "cic_type", ROM_CIC_TYPE_AUTOMATIC);
        if (rom_info->boot_override.cic_type != ROM_CIC_TYPE_AUTOMATIC) {
            rom_info->boot_override.cic = true;
        }

        rom_info->boot_override.save_type = ini_get_int(rom_config_ini, "custom_boot", "save_type", SAVE_TYPE_AUTOMATIC);
        if (rom_info->boot_override.save_type != SAVE_TYPE_AUTOMATIC) {
            rom_info->boot_override.save = true;
        }

        rom_info->boot_override.tv_type = ini_get_int(rom_config_ini, "custom_boot", "tv_type", ROM_TV_TYPE_AUTOMATIC);
        if (rom_info->boot_override.tv_type != ROM_TV_TYPE_AUTOMATIC) {
            rom_info->boot_override.tv = true;
        }

        ini_free(rom_config_ini);
    }

    path_free(rom_info_path);
}

static rom_err_t save_rom_config_setting_to_file (path_t *path, const char *type, const char *id, int value, int default_value) {
    path_t *rom_info_path = path_clone(path);

    path_ext_replace(rom_info_path, "ini");

    ini_t *rom_config_ini = ini_try_load(path_get(rom_info_path));
    if (!rom_config_ini) {
        path_free(rom_info_path);
        return ROM_ERR_SAVE_IO;
    }

    if (value == default_value) {
        ini_delete_key(rom_config_ini, type, id);
    } else {
        ini_set_int(rom_config_ini, type, id, value);
    }

    bool empty = ini_is_empty(rom_config_ini);

    if (!empty) {
        if (!ini_save(rom_config_ini, path_get(rom_info_path))) {
            path_free(rom_info_path);
            ini_free(rom_config_ini);
            return ROM_ERR_SAVE_IO;
        }
    }

    ini_free(rom_config_ini);

    if (empty) {
        if (remove(path_get(rom_info_path)) && (errno != ENOENT)) {
            path_free(rom_info_path);
            return ROM_ERR_SAVE_IO;
        }
    }

    path_free(rom_info_path);

    return ROM_OK;
}


rom_cic_type_t rom_info_get_cic_type (rom_info_t *rom_info) {
    if (rom_info->boot_override.cic) {
        return rom_info->boot_override.cic_type;
    } else {
        return rom_info->cic_type;
    }
}

bool rom_info_get_cic_seed (rom_info_t *rom_info, uint8_t *seed) {
    cic_type_t cic_type;

    switch (rom_info_get_cic_type(rom_info)) {
        case ROM_CIC_TYPE_5101: cic_type = CIC_5101; break;
        case ROM_CIC_TYPE_5167: cic_type = CIC_5167; break;
        case ROM_CIC_TYPE_6101: cic_type = CIC_6101; break;
        case ROM_CIC_TYPE_7102: cic_type = CIC_7102; break;
        case ROM_CIC_TYPE_x102: cic_type = CIC_x102; break;
        case ROM_CIC_TYPE_x103: cic_type = CIC_x103; break;
        case ROM_CIC_TYPE_x105: cic_type = CIC_x105; break;
        case ROM_CIC_TYPE_x106: cic_type = CIC_x106; break;
        case ROM_CIC_TYPE_8301: cic_type = CIC_8301; break;
        case ROM_CIC_TYPE_8302: cic_type = CIC_8302; break;
        case ROM_CIC_TYPE_8303: cic_type = CIC_8303; break;
        case ROM_CIC_TYPE_8401: cic_type = CIC_8401; break;
        case ROM_CIC_TYPE_8501: cic_type = CIC_8501; break;
        default: cic_type = CIC_UNKNOWN; break;
    }

    *seed = cic_get_seed(cic_type);

    return (!rom_info->boot_override.cic);
}

rom_err_t rom_config_override_cic_type (path_t *path, rom_info_t *rom_info, rom_cic_type_t cic_type) {
    rom_info->boot_override.cic = (cic_type != ROM_CIC_TYPE_AUTOMATIC);
    rom_info->boot_override.cic_type = cic_type;

    return save_rom_config_setting_to_file(path, "custom_boot", "cic_type", rom_info->boot_override.cic_type, ROM_CIC_TYPE_AUTOMATIC);
}

rom_save_type_t rom_info_get_save_type (rom_info_t *rom_info) {
    if (rom_info->boot_override.save) {
        return rom_info->boot_override.save_type;
    } else {
        return rom_info->save_type;
    }
}

rom_err_t rom_config_override_save_type (path_t *path, rom_info_t *rom_info, rom_save_type_t save_type) {
    rom_info->boot_override.save = (save_type != SAVE_TYPE_AUTOMATIC);
    rom_info->boot_override.save_type = save_type;

    return save_rom_config_setting_to_file(path, "custom_boot", "save_type", rom_info->boot_override.save_type, SAVE_TYPE_AUTOMATIC);
}

rom_tv_type_t rom_info_get_tv_type (rom_info_t *rom_info) {
    if (rom_info->boot_override.tv) {
        return rom_info->boot_override.tv_type;
    } else {
        return rom_info->tv_type;
    }
}

rom_err_t rom_config_override_tv_type (path_t *path, rom_info_t *rom_info, rom_tv_type_t tv_type) {
    rom_info->boot_override.tv = (tv_type != ROM_TV_TYPE_AUTOMATIC);
    rom_info->boot_override.tv_type = tv_type;

    return save_rom_config_setting_to_file(path, "custom_boot", "tv_type", rom_info->boot_override.tv_type, ROM_TV_TYPE_AUTOMATIC);
}

rom_err_t rom_config_setting_set_cheats (path_t *path, rom_info_t *rom_info, bool enabled) {
    rom_info->settings.cheats_enabled = enabled;
    return save_rom_config_setting_to_file(path, "", "cheats_enabled", enabled, false);
}

rom_err_t rom_config_setting_set_clear_rdram (path_t *path, rom_info_t *rom_info, bool enabled) {
    rom_info->settings.clear_rdram_enabled = enabled;
    return save_rom_config_setting_to_file(path, "", "clear_rdram_enabled", enabled, false);
}

#ifdef FEATURE_PATCHER_GUI_ENABLED
rom_err_t rom_config_setting_set_patches (path_t *path, rom_info_t *rom_info, bool enabled) {
    rom_info->settings.patches_enabled = enabled;
    return save_rom_config_setting_to_file(path, "", "patches_enabled", enabled, false);
}
#endif

rom_err_t rom_config_load (path_t *path, rom_info_t *rom_info) {
    FILE *f;
    rom_header_t rom_header;
    
    debugf("[META] rom_config_load: starting for '%s'\\n", path_get(path));

    if ((f = fopen(path_get(path), "rb")) == NULL) {
        return ROM_ERR_NO_FILE;
    }
    setbuf(f, NULL);
    if (fread(&rom_header, sizeof(rom_header), 1, f) != 1) {
        fclose(f);
        return ROM_ERR_LOAD_IO;
    }
    if (fclose(f)) {
        return ROM_ERR_LOAD_IO;
    }

    fix_rom_header_endianness(&rom_header, rom_info);

    match_t match = find_rom_in_database(&rom_header);

    extract_rom_info(&match, &rom_header, rom_info);
    debugf("[META] rom_config_load: game_code='%c%c%c%c', CIC type=%d\\n", 
           rom_info->game_code[0], rom_info->game_code[1], rom_info->game_code[2], rom_info->game_code[3],
           rom_info->cic_type);

    load_rom_config_from_file(path, rom_info);

    // Try to load metadata from external .meta file first, then from embedded ZIP
    load_rom_meta_from_file(path, rom_info);
    debugf("[META] rom_config_load: after load_rom_meta_from_file, name='%s'\\n", rom_info->meta.name);
    
    // If external .meta load found nothing (name is still empty), try embedded metadata
    if (rom_info->meta.name && strlen(rom_info->meta.name) == 0) {
        debugf("[META] rom_config_load: name is empty, trying embedded metadata\\n");
        load_rom_meta_from_embedded_zip(path_get(path), &rom_header, rom_info);
        debugf("[META] rom_config_load: after load_rom_meta_from_embedded_zip, name='%s'\\n", rom_info->meta.name);
    } else {
        debugf("[META] rom_config_load: external metadata found, skipping embedded\\n");
    }
    
    debugf("[META] rom_config_load: complete\\n");

    return ROM_OK;
}
