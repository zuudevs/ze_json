![](JSON_checker/json160.gif)
# JSON_checker
JSON_checker is a Pushdown Automaton that very quickly determines if a JSON text is syntactically correct. It could be used to filter inputs to a system, or to verify that the outputs of a system are syntactically correct. It could be adapted to produce a very fast JSON parser.

JSON_checker is made up of these files:

| Filename | Description |
|---|---|
| JSON_checker.c | The JSON_checker. |
| JSON_checker.h | The JSON_checker header file. |

**Accessories**

| Filename | Description |
|---|---|
| utf8_to_utf16.c | A UTF-8 to UTF-16 converter. |
| utf8_to_utf16.h | The UTF-8 to UTF-16 converter header file. |
| utf8_decode.c | A UTF-8 decoder. |
| utf8_decode.h | The UTF-8 decoder header file. |

JSON_checker comes with a [test suite](https://www.json.org/JSON_checker/test.zip). If the JSON_checker is working correctly, it must accept all of the `pass*.json` files and reject all of the `fail*.json` files.