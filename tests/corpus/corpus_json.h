/*
 * A minimal JSON reader, for the test suite only.
 *
 * It exists so that tests/test_gr_corpus.c can read the committed corpus
 * research/corpus/gr_golden.json *as committed*, rather than against a
 * generated C table that could drift away from it. The corpus is the artifact
 * whose agreement with the literature was established; a test that reads a
 * transcription of it proves something weaker than a test that reads it.
 *
 * Deliberately strict rather than complete, because a golden corpus is not
 * untrusted input and a silent reinterpretation of one is worse than a refusal:
 *
 *   - numbers are integers only. A decimal or an exponent is a parse error,
 *     since an inexact literal has no place in an exact corpus;
 *   - \u escapes are refused. The two-character escapes are handled; the
 *     corpus is ASCII mathematics and a codepoint escape appearing in it should
 *     stop the test, not be guessed at;
 *   - duplicate object keys are refused, because which one wins would
 *     otherwise silently decide what the test compares against.
 *
 * Never compiled into phy_core and never shipped to the device.
 */
#ifndef PHY_TEST_CORPUS_JSON_H
#define PHY_TEST_CORPUS_JSON_H

#include <stdbool.h>
#include <stddef.h>

typedef enum {
    PHY_JSON_NULL = 0,
    PHY_JSON_BOOL,
    PHY_JSON_INTEGER,
    PHY_JSON_STRING,
    PHY_JSON_ARRAY,
    PHY_JSON_OBJECT
} phy_json_kind;

typedef struct phy_json_value phy_json_value;
typedef struct phy_json_document phy_json_document;

/*
 * Read and parse `path`. Returns NULL on any failure, having written a
 * human-readable reason into `error` when `error_capacity` is non-zero.
 */
phy_json_document *phy_json_read_file(const char *path, char *error,
                                      size_t error_capacity);
void phy_json_document_destroy(phy_json_document *document);

/* The document's single root value. NULL only for a NULL document. */
const phy_json_value *phy_json_root(const phy_json_document *document);

phy_json_kind phy_json_kind_of(const phy_json_value *value);

/* Entries of an array or object; 0 for anything else. */
size_t phy_json_count(const phy_json_value *value);

/* Element `index` of an array, or NULL. */
const phy_json_value *phy_json_element(const phy_json_value *value,
                                       size_t index);

/* Key and value of entry `index` of an object, in document order, or NULL. */
const char *phy_json_key_at(const phy_json_value *value, size_t index);
const phy_json_value *phy_json_value_at(const phy_json_value *value,
                                        size_t index);

/* Member `key` of an object, or NULL when absent or not an object. */
const phy_json_value *phy_json_member(const phy_json_value *value,
                                      const char *key);

/* NULL unless the value is a string. The bytes are NUL-terminated. */
const char *phy_json_string(const phy_json_value *value);

/* False unless the value is an integer, in which case *out receives it. */
bool phy_json_integer(const phy_json_value *value, long *out_number);

/* False unless the value is a boolean, in which case *out receives it. */
bool phy_json_bool(const phy_json_value *value, bool *out_boolean);

#endif /* PHY_TEST_CORPUS_JSON_H */
