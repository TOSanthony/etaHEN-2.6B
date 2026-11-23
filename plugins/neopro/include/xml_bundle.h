#pragma once

typedef struct neo_xml_entry {
    const char *name;
    const char *begin;
    const char *end;
} neo_xml_entry;

extern const neo_xml_entry neo_xml_bundle[];
extern const unsigned neo_xml_bundle_count;
