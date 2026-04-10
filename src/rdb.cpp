#include "rdb.h"
#include "config.h"
#include "store.h"
#include <fstream>

/**
 * @brief
type 0 → length fits in 6 bits, already have it in the same byte (max 63)
type 1 → length needs 14 bits, read 1 more byte (max 16383)
type 2 → length needs 32 bits, read 4 more bytes (max ~4 billion)
type 3 → not a length, it's a special encoding (integer stored as string)



Type 1 — 14 bit length:
42 BC

read 42 = 01000010
top 2 bits = 01 → type 1
remaining 6 bits = 000010
read next byte BC = 10111100
combine: 000010 10111100 = 0000001010111100 = 700
so next string is 700 bytes long


Type 2 — 32 bit length:
80 00 00 42 68

read 80 = 10000000
top 2 bits = 10 → type 2
ignore remaining 6 bits
read next 4 bytes: 00 00 42 68
big-endian: (0 << 24) | (0 << 16) | (0x42 << 8) | 0x68
= 0 + 0 + 16896 + 104 = 17000
so next string is 17000 bytes long


RDB file format example:

52 45 44 49 53 30 30 31 31  ← "REDIS0011" (header)
FA                          ← metadata section
09 72 65 64 69 73 2D 76 65 72  ← "redis-ver" (9 bytes)
06 36 2E 30 2E 31 36        ← "6.0.16" (6 bytes)
FE                          ← database section starts
00                          ← database index 0
FB                          ← hash table size info
01                          ← 1 key in db
00                          ← 0 keys with expiry
00                          ← value type = string
03 66 6F 6F                 ← key: "foo"
03 62 61 72                 ← value: "bar"
FF                          ← end of file
89 3b b7 4e f8 0f 77 19     ← CRC64 checksum



Parsing this file would look like:

read 9 bytes → skip "REDIS0011"

read FA → metadata, skip it:
  read_string → "redis-ver" (discard)
  read_string → "6.0.16" (discard)

read FE → database starts:
  read 1 byte → 00 (db index, discard)

read FB → hash table sizes:
  size_encoded → 1 (discard)
  size_encoded → 0 (discard)

read 00 → value type = string, no expiry
  read_string → "foo" (key)
  read_string → "bar" (value)
  kv_store["foo"] = "bar"

read FF → stop
 */

int size_of_length_encoded_string(std::ifstream &file)
{
    int byte = file.get();
    if (byte == EOF)
        return -1;
    uint8_t b = static_cast<uint8_t>(byte);
    int type = b >> 6;
    if (type == 0)
    {
        return b & 0x3F; // 6 bits for length
    }
    else if (type == 1)
    {
        int next_byte = file.get();
        if (next_byte == EOF)
            return -1;
        return ((b & 0x3F) << 8) | static_cast<uint8_t>(next_byte); // 14 bits for length
    }
    else if (type == 2)
    {
        int len = 0;
        for (int i = 0; i < 4; i++)
        {
            int next_byte = file.get();
            if (next_byte == EOF)
                return -1;
            len |= (static_cast<uint8_t>(next_byte) << (8 * (3 - i)));
        }
        return len; // 32 bits for length
    }
    else if (type == 3)
    {
        return -2; // length encoded as a string follows
    }
    return -1; // invalid encoding
}

void load_rdb(const std::string &dir, const std::string &filename)
{
    std::ifstream rdb_file(dir + "/" + filename, std::ios::binary);
    // skip header redis magic string and version
    rdb_file.seekg(9);

    int byte;
    while ((byte = rdb_file.get()) != EOF)
    {
        uint8_t b = static_cast<uint8_t>(byte);
        if (b == 0xFF)
        { // end of file
            break;
        }
        else if (b == 0xFA)
        { // metadata, skip it
            while (true)
            {
                int len = size_of_length_encoded_string(rdb_file);
                if (len < 0)
                    break;                          // error or end of metadata
                rdb_file.seekg(len, std::ios::cur); // skip the string
            }
        }
        else if (b == 0xFE)
        {                   // database section starts
            rdb_file.get(); // skip db index
        }
        else if (b == 0xFB)
        {                                            // hash table size info, skip it
            size_of_length_encoded_string(rdb_file); // skip hash table size
            size_of_length_encoded_string(rdb_file); // skip number of keys with expiry
        }
        else
        { // it's a key-value pair
            int type = b;
            int key_len = size_of_length_encoded_string(rdb_file);
            std::string key(key_len, '\0');
            rdb_file.read(&key[0], key_len);

            int value_len = size_of_length_encoded_string(rdb_file);
            std::string value(value_len, '\0');
            rdb_file.read(&value[0], value_len);

            kv_store[key] = value;
        }
    }
}