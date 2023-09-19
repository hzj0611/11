/*
 * Unit tests for Patch API functions
 *
 * Copyright 2023 Jeff Smith
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include "wine/test.h"

#include "patchapi.h"

#ifndef PATCH_OPTION_SIGNATURE_MD5
#define PATCH_OPTION_SIGNATURE_MD5 0x01000000
#endif

static BOOL (WINAPI *pNormalizeFileForPatchSignature)(PVOID, ULONG, ULONG, PATCH_OPTION_DATA*, ULONG,
        ULONG, ULONG, PPATCH_IGNORE_RANGE, ULONG, PPATCH_RETAIN_RANGE);

static BOOL (WINAPI *pGetFilePatchSignatureByBuffer)(PBYTE, ULONG, ULONG, PVOID, ULONG,
        PPATCH_IGNORE_RANGE, ULONG, PPATCH_RETAIN_RANGE, ULONG, LPSTR);

static BYTE array[1024];

static void init_function_pointers(void)
{
    HMODULE mspatcha = LoadLibraryA("mspatcha.dll");
    if (!mspatcha)
    {
        win_skip("mspatcha.dll not found\n");
        return;
    }
    pNormalizeFileForPatchSignature = (void *)GetProcAddress(mspatcha, "NormalizeFileForPatchSignature");
    pGetFilePatchSignatureByBuffer = (void *)GetProcAddress(mspatcha, "GetFilePatchSignatureByBuffer");
}

static void test_normalize_ignore_range(void)
{
    PATCH_IGNORE_RANGE ir_bad_good[] = {
        /* Range partially outside the region to normalize */
        {7, 2},
        /* Range within the region to normalize */
        {3, 2}
    };

    BOOL result;

    if (!pNormalizeFileForPatchSignature)
    {
        skip("NormalizeFileForPatchSignature not found\n");
        return;
    }

    /* Skip partially out-of-bounds ignore */
    memcpy(array, "abcdefgh", 8);
    result = pNormalizeFileForPatchSignature(array, 8,  0, NULL,  0, 0,  1, ir_bad_good,  0, NULL);
    todo_wine
    ok(result == 1, "Expected %d, got %d\n", 1, result);
    ok(!memcmp(array, "abcdefgh", 8), "Buffer should not have been modified\n");

    /* Skip partially out-of-bounds ignore, apply in-bounds ignore */
    memcpy(array, "abcdefgh", 8);
    result = pNormalizeFileForPatchSignature(array, 8,  0, NULL,  0, 0,  2, ir_bad_good,  0, NULL);
    todo_wine
    ok(result == 2, "Expected %d, got %d\n", 2, result);
    todo_wine
    ok(!memcmp(array, "abc\0\0fgh", 8), "Buffer not modified correctly\n");

    /* Blanking a region already consisting of 0's is considered a change */
    memset(array, 0, 8);
    result = pNormalizeFileForPatchSignature(array, 8,  0, NULL,  0, 0,  2, ir_bad_good,  0, NULL);
    todo_wine
    ok(result == 2, "Expected %d, got %d\n", 2, result);
    ok(!memcmp(array, "\0\0\0\0\0\0\0\0", 8), "Buffer should not have been modified\n");
}

static void test_normalize_retain_range(void)
{
    PATCH_RETAIN_RANGE rr_bad_good[] = {
        /* Range partially outside the region to normalize */
        {7, 2, 3},
        /* Range within the region to normalize */
        {1, 2, 5}
    };

    BOOL result;

    if (!pNormalizeFileForPatchSignature)
    {
        skip("NormalizeFileForPatchSignature not found\n");
        return;
    }

    /* Skip partially out-of-bounds retain */
    memcpy(array, "abcdefgh", 8);
    result = pNormalizeFileForPatchSignature(array, 8,  0, NULL,  0, 0,  0, NULL,  1, rr_bad_good);
    todo_wine
    ok(result == 1, "Expected %d, got %d\n", 1, result);
    ok(!memcmp(array, "abcdefgh", 8), "Buffer should not have been modified\n");

    /* Skip partially out-of-bounds retain, apply in-bounds retain */
    memcpy(array, "abcdefgh", 8);
    result = pNormalizeFileForPatchSignature(array, 8,  0, NULL,  0, 0,  0, NULL,  2, rr_bad_good);
    todo_wine
    ok(result == 2, "Expected %d, got %d\n", 2, result);
    todo_wine
    ok(!memcmp(array, "a\0\0defgh", 8), "Buffer not modified correctly\n");

    /* Blanking a region already consisting of 0's is considered a change */
    memset(array, 0, 8);
    result = pNormalizeFileForPatchSignature(array, 8,  0, NULL,  0, 0,  0, NULL,  2, rr_bad_good);
    todo_wine
    ok(result == 2, "Expected %d, got %d\n", 2, result);
    ok(!memcmp(array, "\0\0\0\0\0\0\0\0", 8), "Buffer should not have been modified\n");
}

static void setup_pe(PVOID buffer, PIMAGE_NT_HEADERS32 *header)
{
    PIMAGE_DOS_HEADER dos_header = (PIMAGE_DOS_HEADER)buffer;
    PIMAGE_NT_HEADERS32 nt_header = (PIMAGE_NT_HEADERS32)(dos_header + 1);

    dos_header->e_magic = IMAGE_DOS_SIGNATURE;
    dos_header->e_lfanew = sizeof(*dos_header);

    nt_header->Signature = IMAGE_NT_SIGNATURE;
    nt_header->FileHeader.NumberOfSections = 0;
    nt_header->FileHeader.SizeOfOptionalHeader = sizeof(IMAGE_OPTIONAL_HEADER32);
    nt_header->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR32_MAGIC;
    nt_header->OptionalHeader.NumberOfRvaAndSizes = IMAGE_NUMBEROF_DIRECTORY_ENTRIES;

    if (header)
        *header = nt_header;
}

static void setup_pe_with_sections(PVOID buffer, PIMAGE_NT_HEADERS32 *header, PDWORD *reloc_target)
{
    PIMAGE_SECTION_HEADER section_headers;
    PIMAGE_BASE_RELOCATION base_relocation;
    PWORD type_offset;
    DWORD dir_entry = IMAGE_DIRECTORY_ENTRY_BASERELOC;

    DWORD code_rva_base   = 0x1000;
    DWORD reloc_rva_base  = 0x2000;
    DWORD code_raw_base   = 0x200;
    DWORD reloc_raw_base  = 0x300;
    DWORD image_base_init = 0x400000;
    WORD reloc_target_offset = 0x30;

    setup_pe(buffer, header);

    (*header)->FileHeader.NumberOfSections = 2;
    (*header)->OptionalHeader.ImageBase = image_base_init;
    (*header)->OptionalHeader.DataDirectory[dir_entry].VirtualAddress = reloc_rva_base;
    (*header)->OptionalHeader.DataDirectory[dir_entry].Size = 12;
    section_headers = (PIMAGE_SECTION_HEADER)((*header) + 1);
    memcpy(section_headers[0].Name, ".text\0\0\0", 8);
    section_headers[0].Misc.VirtualSize = 0xf2;
    section_headers[0].VirtualAddress = code_rva_base;
    section_headers[0].SizeOfRawData = 0x100;
    section_headers[0].PointerToRawData = code_raw_base;
    memcpy(section_headers[1].Name, ".reloc\0\0", 8);
    section_headers[1].Misc.VirtualSize = 0xf2;
    section_headers[1].VirtualAddress = reloc_rva_base;
    section_headers[1].SizeOfRawData = 0x100;
    section_headers[1].PointerToRawData = reloc_raw_base;
    base_relocation = (PIMAGE_BASE_RELOCATION)(&array[reloc_raw_base]);
    base_relocation->VirtualAddress = code_rva_base;
    base_relocation->SizeOfBlock = 12;
    type_offset = (PWORD)(base_relocation + 1);
    *type_offset = (IMAGE_REL_BASED_HIGHLOW << 12) | reloc_target_offset;

    if (reloc_target)
        *reloc_target = (PDWORD)(&array[code_raw_base + reloc_target_offset]);
}

static void test_normalize_flags(void)
{
    const static struct {
        BOOL init_magic_64;
        DWORD init_checksum;
        DWORD init_timestamp;
        ULONG buffer_size;
        ULONG flags;
        ULONG image_base;
        ULONG timestamp;
        BOOL exp_result;
        DWORD exp_checksum;
        DWORD exp_timestamp;
    } td[] = 
    {
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            512, 0, 0, 0,
            1, 0xdeadbeef, 0xdeadbeef},
        /* No rebase */
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            512, PATCH_OPTION_NO_REBASE, 0, 0,
            1, 0xdeadbeef, 0xdeadbeef},

        /* Blank checksum */
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            512, PATCH_OPTION_NO_CHECKSUM, 0, 0,
            2, 0, 0xdeadbeef},
        /* Blank checksum, no rebase */
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            512, PATCH_OPTION_NO_CHECKSUM | PATCH_OPTION_NO_REBASE, 0, 0,
            2, 0, 0xdeadbeef},
        /* Blank checksum, already 0 */
        {FALSE, 0, 0xdeadbeef,
            512, PATCH_OPTION_NO_CHECKSUM, 0, 0,
            1, 0, 0xdeadbeef},
        /* Blank checksum fail - filesize too small */
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            511, PATCH_OPTION_NO_CHECKSUM, 0, 0,
            1, 0xdeadbeef, 0xdeadbeef},
        /* Blank checksum fail - PE32+ magic */
        {TRUE, 0xdeadbeef, 0xdeadbeef,
            512, PATCH_OPTION_NO_CHECKSUM, 0, 0,
            1, 0xdeadbeef, 0xdeadbeef},

        /* Set timestamp */
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            512, 0, 0, 0x12345678,
            2, 0xa61e, 0x12345678},
        /* Set timestamp, no rebase */
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            512, PATCH_OPTION_NO_REBASE, 0, 0x12345678,
            1, 0xdeadbeef, 0xdeadbeef},
        /* Set timestamp, to same value */
        {FALSE, 0xdeadbeef, 0x12345678,
            512, 0, 0, 0x12345678,
            1, 0xdeadbeef, 0x12345678},
        /* Set timestamp, no_rebase, blank checksum */
        {FALSE, 0xdeadbeef, 0xdeadbeef,
            512, PATCH_OPTION_NO_CHECKSUM | PATCH_OPTION_NO_REBASE, 0, 0x12345678,
            2, 0, 0xdeadbeef},
    };

    PIMAGE_NT_HEADERS32 header;
    BOOL result;
    UINT i;

    if (!pNormalizeFileForPatchSignature)
    {
        skip("NormalizeFileForPatchSignature not found\n");
        return;
    }

    for (i = 0; i < ARRAY_SIZE(td); i++)
    {
        winetest_push_context("%u", i);

        memset(array, 0xcc, 512);
        setup_pe(array, &header);
        if (td[i].init_magic_64)
            header->OptionalHeader.Magic = IMAGE_NT_OPTIONAL_HDR64_MAGIC;
        header->FileHeader.TimeDateStamp = td[i].init_timestamp;
        header->OptionalHeader.CheckSum = td[i].init_checksum;
        result = pNormalizeFileForPatchSignature(array, td[i].buffer_size, td[i].flags, NULL,
            td[i].image_base, td[i].timestamp, 0, NULL, 0, NULL);

        todo_wine
        ok(result == td[i].exp_result, "Expected %d, got %d\n", td[i].exp_result, result);
        todo_wine_if(td[i].exp_timestamp != td[i].init_timestamp)
        ok(header->FileHeader.TimeDateStamp == td[i].exp_timestamp,
            "Expected timestamp %#lx, got %#lx\n",
            td[i].exp_timestamp, header->FileHeader.TimeDateStamp);
        todo_wine_if(td[i].exp_checksum != td[i].init_checksum)
        ok(header->OptionalHeader.CheckSum == td[i].exp_checksum,
            "Expected checksum %#lx, got %#lx\n",
            td[i].exp_checksum, header->OptionalHeader.CheckSum);

        winetest_pop_context();
    }
}

static void test_normalize_rebase(void)
{
    PIMAGE_NT_HEADERS32 header;
    BOOL result;
    PDWORD reloc_target;
    DWORD image_base_initial;
    DWORD image_base_new = 0x500000;
    DWORD reloc_target_value = 0x3fffffff;
    DWORD reloc_target_exp;

    if (!pNormalizeFileForPatchSignature)
    {
        skip("NormalizeFileForPatchSignature not found\n");
        return;
    }

    memset(array, 0, 1024);
    setup_pe_with_sections(array, &header, &reloc_target);
    *reloc_target = reloc_target_value;
    reloc_target_exp = reloc_target_value + (image_base_new - header->OptionalHeader.ImageBase);
    result = pNormalizeFileForPatchSignature(array, 1024, 0, NULL, image_base_new, 0, 0, NULL, 0, NULL);
    todo_wine
    ok(result == 2, "Expected %d, got %d\n", 2, result);
    todo_wine
    ok(header->OptionalHeader.ImageBase == image_base_new, "Expected %#lx, got %#lx\n",
        image_base_new, header->OptionalHeader.ImageBase);
    todo_wine
    ok(*reloc_target == reloc_target_exp, "Expected %#lx, got %#lx\n", reloc_target_exp, *reloc_target);

    /* Relocation table extends beyond virtual size, but within raw data size */
    memset(array, 0, 1024);
    setup_pe_with_sections(array, &header, NULL);
    header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress += 0xf4;
    result = pNormalizeFileForPatchSignature(array, 1024, 0, NULL, image_base_new, 0, 0, NULL, 0, NULL);
    todo_wine
    ok(result == 2, "Expected %d, got %d\n", 2, result);
    todo_wine
    ok(header->OptionalHeader.ImageBase == image_base_new, "Expected %#lx, got %#lx\n",
        image_base_new, header->OptionalHeader.ImageBase);

    /* Relocation table starts within raw data size, but ends beyond */
    memset(array, 0, 1024);
    setup_pe_with_sections(array, &header, NULL);
    image_base_initial = header->OptionalHeader.ImageBase;
    header->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_BASERELOC].VirtualAddress += 0xf8;
    result = pNormalizeFileForPatchSignature(array, 1024, 0, NULL, image_base_new, 0, 0, NULL, 0, NULL);
    todo_wine
    ok(result == 1, "Expected %d, got %d\n", 2, result);
    ok(header->OptionalHeader.ImageBase == image_base_initial, "Expected %#lx, got %#lx\n",
        image_base_initial, header->OptionalHeader.ImageBase);

    /* Relocation table extends beyond end of file */
    memset(array, 0, 1024);
    setup_pe_with_sections(array, &header, NULL);
    image_base_initial = header->OptionalHeader.ImageBase;
    result = pNormalizeFileForPatchSignature(array, 779, 0, NULL, image_base_new, 0, 0, NULL, 0, NULL);
    todo_wine
    ok(result == 1, "Expected %d, got %d\n", 1, result);
    ok(header->OptionalHeader.ImageBase == image_base_initial, "Expected %#lx, got %#lx\n",
        image_base_initial, header->OptionalHeader.ImageBase);
}

static void test_signature_by_buffer(void)
{
    PIMAGE_NT_HEADERS32 header;
    BOOL result;
    DWORD err;
    char buf[33];

    if (!pGetFilePatchSignatureByBuffer)
    {
        skip("NormalizeFileForPatchSignature not found\n");
        return;
    }

    /* Test CRC32 signature */
    memset(array, 0xcc, 8);
    buf[0] = '\0';
    result = pGetFilePatchSignatureByBuffer(array, 8, 0, NULL, 0, NULL, 0, NULL, 9, buf);
    todo_wine
    ok(result == TRUE, "Expected %d, got %d\n", TRUE, result);
    todo_wine
    ok(!strcmp(buf, "58ea8bb8"), "Expected %s, got %s\n", "58ea8bb8", buf);

    /* Test CRC32 signature w/ insufficient buffer */
    memset(array, 0xcc, 8);
    buf[0] = '\0';
    SetLastError(0xdeadbeef);
    result = pGetFilePatchSignatureByBuffer(array, 8, 0, NULL, 0, NULL, 0, NULL, 8, buf);
    err = GetLastError();
    ok(result == FALSE, "Expected %d, got %d\n", FALSE, result);
    todo_wine
    ok(err == ERROR_INSUFFICIENT_BUFFER, "Expected ERROR_INSUFFICIENT_BUFFER, got %#lx\n", err);
    ok(!buf[0], "Got unexpected %s\n", buf);

    /* Test MD5 signature */
    memset(array, 0xcc, 8);
    buf[0] = '\0';
    result = pGetFilePatchSignatureByBuffer(array, 8, PATCH_OPTION_SIGNATURE_MD5, NULL,
        0, NULL, 0, NULL, 33, buf);
    todo_wine
    ok(result == TRUE, "Expected %d, got %d\n", TRUE, result);
    todo_wine
    ok(!strcmp(buf, "7bffa66e1c861fcbf38426d134508908"), "Expected %s, got %s\n",
        "7bffa66e1c861fcbf38426d134508908", buf);

    /* Test MD5 signature w/ insufficient buffer */
    memset(array, 0xcc, 8);
    buf[0] = '\0';
    SetLastError(0xdeadbeef);
    result = pGetFilePatchSignatureByBuffer(array, 8, PATCH_OPTION_SIGNATURE_MD5, NULL,
        0, NULL, 0, NULL, 32, buf);
    err = GetLastError();
    ok(result == FALSE, "Expected %d, got %d\n", FALSE, result);
    todo_wine
    ok(err == ERROR_INSUFFICIENT_BUFFER, "Expected ERROR_INSUFFICIENT_BUFFER, got %#lx\n", err);
    ok(!buf[0], "Got unexpected %s\n", buf);

    /* Test signature of PE32 executable image */
    memset(array, 0, 1024);
    setup_pe_with_sections(array, &header, NULL);
    header->FileHeader.TimeDateStamp = 0xdeadbeef;
    header->OptionalHeader.CheckSum = 0xdeadbeef;
    header->OptionalHeader.ImageBase = 0x400000;
    result = pGetFilePatchSignatureByBuffer(array, 1024, 0, NULL, 0, NULL, 0, NULL, 9, buf);
    todo_wine
    ok(result == TRUE, "Expected %d, got %d\n", TRUE, result);
    todo_wine
    ok(!strcmp(buf, "f953f764"), "Expected %s, got %s\n", "f953f764", buf);
    todo_wine
    ok(header->FileHeader.TimeDateStamp == 0x10000000, "Expected %#x, got %#lx\n",
        0x10000000, header->FileHeader.TimeDateStamp);
    todo_wine
    ok(header->OptionalHeader.CheckSum == 0x9dd2, "Expected %#x, got %#lx\n",
        0x9dd2, header->OptionalHeader.CheckSum);
    todo_wine
    ok(header->OptionalHeader.ImageBase == 0x10000000, "Expected %#x, got %#lx\n",
        0x10000000, header->OptionalHeader.ImageBase);
}

START_TEST(signature)
{
    init_function_pointers();

    test_normalize_ignore_range();
    test_normalize_retain_range();
    test_normalize_flags();
    test_normalize_rebase();
    test_signature_by_buffer();
}
