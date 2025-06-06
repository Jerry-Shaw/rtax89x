import globals from "globals";

export default [
    {files: ["**/*.js"], languageOptions: {sourceType: "script"}},
    {languageOptions: {globals: globals.browser}},
    {
        rules: {
            "no-debugger": "error",
        }
    }
];