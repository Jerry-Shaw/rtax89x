import globals from "globals";

export default [
    {files: ["**/*.js"], languageOptions: {sourceType: "script"}},
    {languageOptions: {globals: globals.browser}},
    {
        rules: {
            "no-debugger": "error",
            "no-dupe-args": "error",
            "no-dupe-keys": "error",
            "no-duplicate-case": "error",
            "no-cond-assign": ["error","except-parens"],
            "valid-typeof": "error",
            "no-extra-semi": "error",
            "no-empty": "error",
            "no-extra-boolean-cast": "error",
            // "no-redeclare": "error",
            // "no-undef": "error",
            // "no-with": "error",
        }
    }
];