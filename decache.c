#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <stdbool.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>

#include "dyld_cache_format.h"
#include "loader.h"

#include <mach-o/nlist.h>
#include <mach-o/stab.h>

#define DEBUG(x, ...) do { printf("DEBUG: " x " (%s @ %s:%d)\n", ##__VA_ARGS__, __FUNCTION__, __FILE__, __LINE__); } while (0)

static
char *_filename = NULL;

static
char *_extract_image = NULL;

static
char *_output_extract_image_file = NULL;

static
bool verbose = false;

static
bool dump_dir = false;

static
void _usage(const char *exename)
{
    fprintf(stderr, "Usage: %s -h -v [dyld_shared_cache_...] {[file to extract] [output filename]}\n", exename);
    fprintf(stderr, "   -D   - dump a directory of all shared images in the dyld_shared_cache\n");
    exit(EXIT_SUCCESS);
}

static
void _dump_dir(struct dyld_cache_header *hdr, const void *cache, size_t cache_len)
{
    const struct dyld_cache_image_info *info = cache + hdr->imagesOffset;

    printf("Directory of Images contains %u images\n", hdr->imagesCount);

    for (size_t i = 0; i < hdr->imagesCount; i++) {
        printf(" 0x%016lx  %8lu %s\n", info[i].address, (unsigned long)0, (const char *)cache + info[i].pathFileOffset);
    }
}

static
int _append_to_file(int fd, const void *buf, size_t len, off_t *poffset)
{
    int ret = -1;

    /* Seek to the end of the file, and get the actual offset in bytes */
    if (0 > (*poffset = lseek(fd, 0, SEEK_END))) {
        fprintf(stderr, "Failure while preparing to append segment data, aborting.\n");
        goto done;
    }

    if (len != 0 && (0 > write(fd, buf, len))) {
        fprintf(stderr, "Failed to write %zu bytes to the output file, aborting.\n", len);
        goto done;
    }

    ret = 0;
done:
    return ret;
}

/**
 * Perform fixups on the Mach-O object to rebase to 0 (for the new file we're writing out)
 */
static
int _fixup_macho_object64(int fd, const void *cache, size_t cache_len, void *macho_hdr, size_t macho_hdr_len, uint64_t base_addr)
{
    int ret = -1;
    struct mach_header_64 *hdr = macho_hdr;
    struct load_command *cmd = macho_hdr + sizeof(struct mach_header_64); /* First load command */
    struct segment_command_64 *linkedit = NULL;
    size_t next_cmd = 0,
           file_base = 0,
           linkedit_file_base = 0;
    off_t linkedit_end = 0;

    /* Prepare fixups for the header */
    while (next_cmd < hdr->sizeofcmds) {
        DEBUG("  0x%x -> %u bytes", cmd->cmd, cmd->cmdsize);

        if (cmd->cmd == LC_SEGMENT_64) {
            /* Fixup the LC_SEGMENT_64 commands, and the constituent sections */
            struct segment_command_64 *seg = (struct segment_command_64 *)cmd;
            uint64_t cache_off = seg->fileoff;
            off_t file_off = -1;
            struct section_64 *sections = (struct section_64 *)((uint8_t *)seg + sizeof(struct segment_command_64));

            DEBUG("    LC_SEGMENT_64: fileoff = 0x%016lx filesize = %lu nsects = %u [%s]",
                    seg->fileoff, seg->filesize, seg->nsects, seg->segname);

            if (!strncmp(seg->segname, SEG_LINKEDIT, 10)) {
                DEBUG("            NOTE: this is the _LINKEDIT section, holding on for later use");
                linkedit = seg;
                linkedit->fileoff = file_base;
                linkedit->filesize = 0;
                /* Skip writing out the LINKEDIT section, we'll reconstruct it as we need to */

                next_cmd += cmd->cmdsize;
                cmd = macho_hdr + sizeof(struct mach_header_64) + next_cmd;
                continue;
            }

            for (size_t i = 0; i < seg->nsects; i++) {
                DEBUG("        [%zu] - 0x%016lx %zu -> %u in file (%08x, reloff %08x) [%s]",
                        i, sections[i].addr, sections[i].size, sections[i].offset, sections[i].offset,
                        sections[i].reloff, sections[i].sectname);

                sections[i].offset = sections[i].offset - cache_off + file_base;
            }

            /* Write the segment from the cache to the file */
            if (0 > (file_off = lseek(fd, 0, SEEK_CUR))) {
                fprintf(stderr, "Failure while preparing to write out segment data, aborting.\n");
                goto done;
            }

            if (0 > write(fd, cache + cache_off, seg->filesize)) {
                fprintf(stderr, "Failed to write %lu bytes to the output file, aborting.\n", seg->filesize);
                goto done;
            }

            seg->fileoff = file_off;
            file_base = file_off + seg->filesize;
        } else if (cmd->cmd == LC_SYMTAB) {
            struct symtab_command *symtab = (struct symtab_command *)cmd;
            off_t file_off = -1;
            const struct nlist_64 *sym = cache + symtab->symoff;
            struct nlist_64 *new_syms = NULL;
            size_t strtab_len = 0;
            const char *strtab = cache + symtab->stroff;

            DEBUG("    LC_SYMTAB: nsyms = %u symoff = %08x stroff = %08x, strsize = %u",
                    symtab->nsyms, symtab->symoff, symtab->stroff, symtab->strsize);

            if (NULL == (new_syms = malloc(symtab->nsyms * sizeof(struct nlist_64)))) {
                fprintf(stderr, "Out of memory; could not allocate memory for %u symbols\n",
                        symtab->nsyms);
                goto done;
            }

            memcpy(new_syms, sym, symtab->nsyms * sizeof(struct nlist_64));

            if (0 > (file_off = lseek(fd, 0, SEEK_CUR))) {
                fprintf(stderr, "Error while getting current file position, aborting.\n");
                goto done;
            }

            symtab->stroff = file_off;

            /* Reconstruct the string table, from the symbol table */
            for (size_t i = 0; i < symtab->nsyms; i++) {
                const char *str = &strtab[sym[i].n_un.n_strx];
                size_t str_len = strlen(str) + 1;

                DEBUG("Symbol: 0x%016lx [%s] (%zu bytes)", sym[i].n_value, str, str_len);

                if (0 > _append_to_file(fd, str, str_len, &file_off)) {
                    fprintf(stderr, "Failed to append symbol table string table, aborting.\n");
                    goto done;
                }
                new_syms[i].n_un.n_strx = file_off - symtab->stroff;
                strtab_len += str_len;
            }

            symtab->strsize = strtab_len;

            /* Write out our updated symbol table */
            if (0 > _append_to_file(fd, new_syms, symtab->nsyms * sizeof(struct nlist_64), &file_off)) {
                fprintf(stderr, "Failed to append symbol table, aborting.\n");
                goto done;
            }
            symtab->symoff = file_off;

            DEBUG("    LC_SYMTAB (after): nsyms = %u symoff = %08x stroff = %08x, strsize = %u",
                    symtab->nsyms, symtab->symoff, symtab->stroff, symtab->strsize);
        } else if (cmd->cmd == LC_DYSYMTAB) {
            struct dysymtab_command *dst = (struct dysymtab_command *)cmd;

            DEBUG("    LC_DYSYMTAB: ilocalsym = %08x, iextdefsym = %08x, iundefsym = %08x, tocoff = %08x",
                    dst->ilocalsym, dst->iextdefsym, dst->iundefsym, dst->tocoff);
            DEBUG("                 tocoff = %08x, modtaboff = %08x, extrefsymoff = %08x, indirectsymoff = %08x",
                    dst->tocoff, dst->modtaboff, dst->extrefsymoff, dst->indirectsymoff);
            DEBUG("                 extreloff = %08x, locreloff = %08x",
                    dst->extreloff, dst->locreloff);

            /* Fix up the indirect symbol offset */
            if (0 != dst->indirectsymoff) {
                off_t file_off = 0;
                if (0 > _append_to_file(fd, cache + dst->indirectsymoff, dst->nindirectsyms * sizeof(uint32_t), &file_off)) {
                    fprintf(stderr, "Failed to write out indirect symbol table, aborting.\n");
                    goto done;
                }
                dst->indirectsymoff = file_off;
            }
        } else if ((cmd->cmd & 0xff) == LC_DYLD_INFO) {
            struct dyld_info_command *dyl = (struct dyld_info_command *)cmd;

            DEBUG("    LC_DYLD_INFO: rebase_off = %08x, bind_off = %08x, weak_bind_off = %08x, lazy_bind_off = %08x, export_off = %08x",
                    dyl->rebase_off, dyl->bind_off, dyl->weak_bind_off, dyl->lazy_bind_off, dyl->export_off);
        } else if (cmd->cmd == LC_FUNCTION_STARTS || cmd->cmd == LC_DATA_IN_CODE) {
            struct linkedit_data_command *dcmd = (struct linkedit_data_command *)cmd;
            off_t file_off = 0;

            DEBUG("    LinkEdit Data (%02x): dataoff = %08x datasize = %u", cmd->cmd, dcmd->dataoff, dcmd->datasize);

            if (0 > _append_to_file(fd, cache + dcmd->dataoff, dcmd->datasize, &file_off)) {
                fprintf(stderr, "Failed to append __LINKEDIT data to file, aborting.");
            }
            dcmd->dataoff = file_off;
        }

        next_cmd += cmd->cmdsize;
        cmd = macho_hdr + sizeof(struct mach_header_64) + next_cmd;
    }

    if (0 > (linkedit_end = lseek(fd, 0, SEEK_END))) {
        fprintf(stderr, "Failed to seek to end of file, aborting.\n");
        goto done;
    }

    /* Update the __LINKEDIT segment size */
    linkedit->filesize = linkedit_end - linkedit->fileoff;

    /* Now write out the updated header */
    if (0 > lseek(fd, 0, SEEK_SET)) {
        fprintf(stderr, "Failed to seek to top of file, aborting.\n");
        goto done;
    }

    if (0 > write(fd, macho_hdr, macho_hdr_len)) {
        fprintf(stderr, "Failed to write out Mach-O header, aborting.\n");
        goto done;
    }

    ret = 0;
done:
    return ret;
}

static
int _dump_file(struct dyld_cache_header *hdr, const void *cache, size_t cache_len,
               const char *image_name, const char *image_out_file_name)
{
    int ret = -1;

    const struct dyld_cache_image_info *info = cache + hdr->imagesOffset,
                                       *image_info = NULL,
                                       *next_image_info = NULL;
    const struct dyld_cache_mapping_info *mapping = cache + hdr->mappingOffset,
                                         *image_mapping = NULL;
    const struct mach_header_64 *header = NULL;
    uint64_t file_offset = 0;
    uint32_t magic = 0;
    void *macho_obj = NULL;
    size_t macho_len = 0;
    int fd = -1;

    DEBUG("Going through list of %u images", hdr->imagesCount);

    for (size_t i = 0; i < hdr->imagesCount; i++) {
        if (NULL != image_info) {
            next_image_info = &info[i];
            break;
        }

        if (!strcmp(image_name, (const char *)cache + info[i].pathFileOffset)) {
            DEBUG("Found target image offset = %016lx!", info[i].address);
            image_info = &info[i];
        }
    }

    if (NULL == image_info) {
        fprintf(stderr, "Unable to find image file: %s\n", image_name);
        goto done;
    }

    /* Figure out what mapping the header is in */
    for (size_t i = 0; i < hdr->mappingCount; i++) {
        image_mapping = &mapping[i];
        if (mapping[i].address < image_info->address) {
            break;
        }
    }


    DEBUG("Using mapping: 0x%016lx (%lu bytes, %lu offset in file)",
            image_mapping->address, image_mapping->size, image_mapping->fileOffset);

    file_offset = image_info->address - image_mapping->address;

    DEBUG("File offset is 0x%016lx", file_offset);

    magic = *(uint32_t *)(cache + file_offset);

    if (magic != MH_MAGIC_64) {
        fprintf(stderr, "This Mach-O file is 32-bits, but we only support 64-bit Mach-O, sorry.\n");
        goto done;
    }

    if (NULL == next_image_info) {
        fprintf(stderr, "Last file in the image, need to fix extracting for this case.\n");
        goto done;
    }

    /* Grab the header so we can calculate the length of the Mach-O load commands and such */
    header = cache + file_offset;

    macho_len = sizeof(struct mach_header_64) + header->sizeofcmds;

    if (NULL == (macho_obj = malloc(macho_len))) {
        fprintf(stderr, "Out of memory. Failed to allocate %zu bytes\n", macho_len);
        goto done;
    }

    DEBUG("Mach-O file header length: %zu bytes", macho_len);

    /* TODO: proper check if we are are outside a valid mapping */

    /* Copy out the header, we'll be tweaking it */
    memcpy(macho_obj, cache + file_offset, macho_len);

    if (0 > (fd = open(image_out_file_name, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH))) {
        fprintf(stderr, "Failed to create output file %s (reason: %s)\n", image_out_file_name,
                strerror(errno));
        goto done;
    }

    if (_fixup_macho_object64(fd, cache, cache_len, macho_obj, macho_len, image_info->address)) {
        fprintf(stderr, "Failure while fixing up loader commands for Mach-O object.");
        goto done;
    }

    if (0 > write(fd, macho_obj, macho_len)) {
        fprintf(stderr, "Failed to write Mach-O file out (reason: %s)\n", strerror(errno));
        goto done;
    }

    ret = 0;
done:
    if (NULL != macho_obj) {
        free(macho_obj);
        macho_obj = NULL;
    }

    if (-1 != fd) {
        close(fd);
        fd = -1;
    }

    return ret;
}

static
int _parse_args(int argc, char * const *argv)
{
    int ret = -1;

    int c = -1;

    while (-1 != (c = getopt(argc, argv, "hvD"))) {
        switch (c) {
        case 'h':
            _usage(argv[0]);
            break;
        case 'v':
            verbose = true;
            break;
        case 'D':
            dump_dir = true;
            break;
        default:
            fprintf(stderr, "Warning: unknown argument: '%c'\n", (char)c);
        }
    }

    if (optind >= argc) {
        fprintf(stderr, "Failure: no filename specified, aborting.\n");
        goto done;
    }

    _filename = strdup(argv[optind]);

    if (argc - optind == 3) {
        _extract_image = strdup(argv[optind + 1]);
        _output_extract_image_file = strdup(argv[optind + 2]);
        printf("Writing '%s' to output file '%s'\n", _extract_image, _output_extract_image_file);
    } else if (argc - optind != 1) {
        fprintf(stderr, "Failure: extra arguments found, aborting.\n");
        goto done;
    }

    ret = 0;
done:
    return ret;
}

int main(int argc, char * const *argv)
{
    int ret = EXIT_FAILURE;

    int fd = -1;
    struct dyld_cache_header hdr;
    struct dyld_cache_mapping_info *mapping;
    struct stat ss;
    void *file = NULL;
    size_t file_len = 0;

    printf("decache - extract Mach-O dylib files from the dyld_shared_cache* files\n");

    if (_parse_args(argc, argv)) {
        _usage(argv[0]);
    }

    DEBUG("Reading from file: %s", _filename);

    fd = open(_filename, O_RDONLY);
    if (0 > fd) {
        fprintf(stderr, "Failure: could not open file %s, aborting\n", _filename);
        goto done;
    }

    if (sizeof(hdr) != read(fd, (void *)&hdr, sizeof(hdr))) {
        fprintf(stderr, "Failure: could not read dyld_cache_header!\n");
        goto done;
    }

    if (strncmp(hdr.magic, "dyld_v1  ", 9)) {
        fprintf(stderr, "Failure: invalid magic.\n");
        goto done;
    }

    DEBUG("Header:");
    DEBUG("  mappingOffset:     0x%016x", hdr.mappingOffset);
    DEBUG("  mappingCount:      0x%u", hdr.mappingCount);
    DEBUG("  imagesOffset:      0x%016x", hdr.imagesOffset);
    DEBUG("  imagesCount:       %u", hdr.imagesCount);
    DEBUG("  dyldBaseAddress:   0x%016lx", hdr.dyldBaseAddress);

    if (0 > fstat(fd, &ss)) {
        fprintf(stderr, "Failed to stat file %s, aborting.\n", _filename);
        goto done;
    }

    /* mmap the whole file */
    file_len = ss.st_size;

    if (MAP_FAILED == (file = mmap(NULL, file_len, PROT_READ, MAP_PRIVATE, fd, 0))) {
        fprintf(stderr, "Failed to mmap(2) file %s. Reason: %s (%d)\n",
                _filename, strerror(errno), errno);
        goto done;
    }

    /* Grab the mapping info */
    mapping = file + hdr.mappingOffset;
    DEBUG("Mappings:");
    for (size_t i = 0; i < hdr.mappingCount; i++) {
        DEBUG("  %02zu  %016lx %10lu bytes -> offset %016lx",
                i, mapping[i].address, mapping[i].size, mapping[i].fileOffset);
    }

    if (dump_dir) {
        DEBUG("Dumping the directory!");
        _dump_dir(&hdr, file, file_len);
    }

    if (NULL != _extract_image && NULL != _output_extract_image_file) {
        if (_dump_file(&hdr, file, file_len, _extract_image, _output_extract_image_file)) {
            fprintf(stderr, "Failure while extracting image file; aborting.\n");
            goto done;
        }
    }

    ret = EXIT_SUCCESS;
done:
    if (NULL != file) {
        munmap(file, file_len);
        file = NULL;
    }

    if (NULL != _extract_image) {
        free(_extract_image);
        _extract_image = NULL;
    }

    if (NULL != _output_extract_image_file) {
        free(_output_extract_image_file);
        _output_extract_image_file = NULL;
    }

    if (NULL != _filename) {
        free(_filename);
        _filename = NULL;
    }

    if (-1 != fd) {
        close(fd);
        fd = -1;
    }

    return ret;
}

