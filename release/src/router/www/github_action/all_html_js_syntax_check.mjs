import fs from 'fs';
import {ESLint} from 'eslint';
import * as core from '@actions/core';
import * as cheerio from 'cheerio';
import path from 'path';
import {fileURLToPath} from 'url';
import readline from 'readline';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

function parseDictionary(filename) {
    const dict = {};
    const data = fs.readFileSync(filename, 'utf8');
    const lines = data.split('\n');
    lines.forEach(line => {
        const [key, value] = line.split('=');
        if (!key.startsWith('[###MODEL_DEP###]')) {
            dict[key.trim()] = value;
        }
    });
    return dict;
}

// 替換檔案中的內容
function replaceContent(content, replacements) {
    Object.keys(replacements).forEach(key => {
        const regex = new RegExp(`<#${key}#>`, 'g');
        content = content.replace(regex, replacements[key]);
    });
    content = content.replace(/"<%.*?%>"/g, '""');
    content = content.replace(/'<%.*?%>'/g, '\'\'');
    content = content.replace(/<%.*?%>/g, '""');
    return content;
}

// 遞迴地讀取目錄中的所有檔案
function readDirectoryRecursive(directory, ignore = []) {
    let files = [];
    const items = fs.readdirSync(directory);
    items.forEach(item => {
        const itemPath = path.join(directory, item);
        const stats = fs.statSync(itemPath);
        if (!ignore.some(ignorePath => itemPath.includes(ignorePath))) {
            const stats = fs.statSync(itemPath);
            if (stats.isFile()) {
                files.push(itemPath);
            } else if (stats.isDirectory()) {
                // 如果是目錄，則遞迴地讀取其內容
                const subFiles = readDirectoryRecursive(itemPath, ignore);
                files = files.concat(subFiles);
            }
        }
    });
    return files;
}

async function main() {

    const errors = [];
    const directory = './';

    const eslint = new ESLint({
        overrideConfigFile: 'all_html_js_eslint.config.mjs'
    });

    function countLines(text) {
        return (text.match(/\r\n|\r|\n/g) || []).length + 1;
    }

    // const dists = ['BR', 'CN', 'CZ', 'DA', 'DE', 'EN', 'ES', 'FI', 'FR', 'HU', 'IT', 'JP', 'KR', 'MS', 'NL', 'NO', 'PL', 'RO', 'RU', 'SL', 'SV', 'TH', 'TR', 'TW', 'UK'];
    const dists = ['EN'];

    const ignoreSrcFiles = ['jquery-ui.js', 'loader.js', 'jquery.js', 'jquery.min.js', 'jquery-ui.min.js', 'chart.js', 'jscolor.js', 'jquery.mobile.js', 'md5.js', 'jstree.min.js', 'require.min.js'];
    const ignoreFiles = ['app_client_stats_page.html'];
    const ignoreFolders = ['node_modules', 'charts', 'sysdep', 'dashboard', 'fbwifi', 'mobile'];

    for (const dist of dists) {
        const dictFilepath = path.join(directory, `${dist}.dict`);
        const dictionary = parseDictionary(dictFilepath);
        console.log(`#Dist: ${dist}`);

        const files = readDirectoryRecursive(directory, ignoreFolders);
        // const files = fs.readdirSync(directory);
        for (const file of files) {
            if ((path.extname(file) === '.asp' || path.extname(file) === '.html' || path.extname(file) === '.htm') && !ignoreFiles.includes(file)) {

                // console.log('File:', file);
                process.stdout.write(`[${dictFilepath}] File: ${file}`);
                const filepath = path.join(__dirname, file);
                // console.log('filepath:', filepath);
                const directoryPath = path.dirname(filepath);
                const originalContent = fs.readFileSync(filepath, 'utf8');

                const $ = cheerio.load(originalContent);

                const lines = originalContent.split('\n');

                const scriptLineNumbers = [];
                lines.forEach((line, index) => {
                    if (line.includes('<script')) {
                        scriptLineNumbers.push(index + 1); // +1 because line numbers are 1-based
                    }
                });

                const scripts = [];
                await $('script').each((i, elem) => {
                    const src = $(elem).attr('src') || file;
                    let scriptContent = $(elem).html() || '';
                    let fileLines = 0;
                    let scriptStartLine = scriptLineNumbers[i] || 0;
                    let combinedFileLines = scripts.reduce((sum, item) => sum + item.fileLines, 0) + i + 1;
                    let fileDesc = `/* ##### File:${src},ScriptStartLine:${scriptStartLine},ScriptLineCount:${fileLines},CombinedFileLines:${combinedFileLines} ##### */`
                    if (src !== file) {
                        let filepath = path.join(directoryPath, src);
                        if (src.charAt(0) === '/') {
                            filepath = path.join(__dirname, src);
                        }
                        if (src.includes('SDN')) {
                            filepath = path.join(__dirname + '/sysdep/FUNCTION/SDN/', src);
                        }
                        // console.log(filepath)
                        try {
                            scriptContent = fs.readFileSync(filepath, 'utf-8');
                            fileLines = countLines(scriptContent);
                            if (ignoreSrcFiles.some(ignoreFile => src.includes(ignoreFile))) {
                                scriptContent = `/* eslint-disable */ ${fileDesc}\n${scriptContent} /* eslint-enable */`;
                            } else {
                                scriptContent = `${fileDesc}\n${scriptContent}`;
                            }
                        } catch (error) {
                            readline.cursorTo(process.stdout, 0);
                            readline.clearLine(process.stdout, 0);
                            console.log('\x1b[31m%s\x1b[0m', `[${dictFilepath}] File: ${file} -> Fail!`)
                            console.error('File not found:', error);
                        }
                    } else {
                        fileLines = countLines(scriptContent);
                        scriptContent = `${fileDesc}\n${scriptContent}`;
                    }

                    // console.log({src, fileLines, scriptStartLine, combinedFileLines})

                    scripts.push({src, content: scriptContent, fileLines, scriptStartLine, combinedFileLines});
                });

                const combinedContent = scripts.map(script => script.content).join('\n');
                // fs.writeFileSync('combined.js', combinedContent, 'utf8');

                const replacedContent = replaceContent(combinedContent, dictionary);

                // fs.writeFileSync('combined2.js', replacedContent, 'utf8');
                await eslint.lintText(replacedContent).then(([result]) => {
                    if (!(result.errorCount === 0 && result.warningCount === 0)) {
                        result.messages.forEach(message => {
                            if (!message.message.includes('Unused eslint-disable directive')) {
                                const errorLineNumber = message.line;
                                const scriptObject = scripts.slice().reverse().find(obj => obj.combinedFileLines < errorLineNumber);
                                let realErrorLineNumber = errorLineNumber - scriptObject.combinedFileLines + scriptObject.scriptStartLine - 1;

                                if (!file.includes(scriptObject.src)) {
                                    realErrorLineNumber = errorLineNumber - scriptObject.combinedFileLines;
                                }

                                const lines = combinedContent.split('\n');
                                const selectedLines = lines.slice(message.line - 2, message.line + 1);
                                const selectedContent = selectedLines.map(line => line.replace(/\t/g, ' '));
                                readline.cursorTo(process.stdout, 0);
                                readline.clearLine(process.stdout, 0);
                                console.log('\x1b[31m%s\x1b[0m', `[${dictFilepath}] File: ${file} -> Fail!`)
                                console.error(`Lang: ${dictFilepath}, File: ${file}, JsFile: ${scriptObject.src}, Line: ${realErrorLineNumber}\n\tJavaScript syntax error: ${message.message} \n\tCode: ${selectedContent}`);
                                errors.push({
                                    dist,
                                    file: scriptObject.src,
                                    line: realErrorLineNumber,
                                    column: message.column,
                                    message: message.message,
                                    // info: message,
                                    code: selectedContent,
                                });
                            }
                        });
                    } else {
                        readline.cursorTo(process.stdout, 0);
                        readline.clearLine(process.stdout, 0);
                        console.log('\x1b[32m%s\x1b[0m', `[${dictFilepath}] File: ${file} -> Pass!`)
                    }
                }).catch(error => {
                    console.error('JavaScript syntax error:', error);
                });
            }

        }
    }


    if (errors.length > 0) {
        throw errors;
    }
}

main()
    .then(() => {
        console.log('All JavaScript syntax check pass!');
    })
    .catch(error => {
        console.error('Some error occurred:', error);
        core.setFailed(error);
    });
