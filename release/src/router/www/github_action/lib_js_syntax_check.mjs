import fs from 'fs';
import {ESLint} from 'eslint';
import path from 'path';
import * as core from '@actions/core';
import {Octokit} from "octokit";

const __dirname = import.meta.dirname;

// 讀取字典檔並解析成 JavaScript 物件
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

    const eslint = new ESLint({
        overrideConfigFile: 'lib_eslint.config.mjs'
    });

    const libraryList = [
        "/js/jquery.js",
        "/calendar/jquery-ui.js",
        "/js/jstree/jstree.min.js",
        "/js/jstree/jstree_customize.js",
        "/md5.js",
        "/state.js",
        "/general.js",
        "/popup.js",
        "/help.js",
        "/validator.js",
        "/js/httpApi.js",
        "/client_function.js",
        "/switcherplugin/jquery.iphone-switch.js",
        "/form.js",
        "/disk_functions.js",
        "/require/require.min.js",
        "/require/modules/makeRequest.js",
        "/require/modules/menuTree.js",
        "/require/modules/amesh.js",
        "/require/modules/diskList.js",
        "/js/support_site.js",
        "/help_content.js",
        "/notification.js",
        "/form.js",
        "/js/collected_FAQ.js",
        "/js/asus_clientlist.js",
        "/js/asus_policy.js",
        "/js/asus_pincode.js",
        "/js/asus_notice.js",
    ];

    function countLines(text) {
        return (text.match(/\r\n|\r|\n/g) || []).length + 1;
    }

    function concatFiles(fileList) {
        let combinedContent = '';
        let fileLinesInfo = [];
        let totalLines = 0;
        fileList.forEach(filePath => {
            const fullPath = path.join(__dirname, filePath);
            if (fs.existsSync(fullPath)) {
                const fileContent = fs.readFileSync(fullPath, 'utf-8');
                const fileLines = countLines(fileContent);
                fileLinesInfo.push({filePath, startLine: totalLines + 1, endLine: totalLines + fileLines});
                totalLines += fileLines;
                combinedContent += fileContent + '\n';
            } else {
                console.error(`File not found: ${fullPath}`);
            }
        });
        return {combinedContent, fileLinesInfo};
    }

    const {combinedContent, fileLinesInfo} = concatFiles(libraryList);


    const directory = './'; // 指定目錄
    // const dists = ['BR', 'CN', 'CZ', 'DA', 'DE', 'EN', 'ES', 'FI', 'FR', 'HU', 'IT', 'JP', 'KR', 'MS', 'NL', 'NO', 'PL', 'RO', 'RU', 'SL', 'SV', 'TH', 'TR', 'TW', 'UK'];
    const dists = ['EN'];

    const ignore = ['node_modules', 'charts'];

    const errors = [];

    for (const dist of dists) {
        const dictFilepath = path.join(directory, `${dist}.dict`);
        const dictionary = parseDictionary(dictFilepath);
        console.log(`#Dist: ${dist}`);

        const replacedCombinedContent = replaceContent(combinedContent, dictionary);
        fs.writeFileSync('combined.js', replacedCombinedContent, 'utf8');
        await eslint.lintText(replacedCombinedContent).then(([result]) => {
            if (!(result.errorCount === 0 && result.warningCount === 0)) {
                result.messages.forEach(message => {
                    const errorFile = fileLinesInfo.find(file => file.startLine <= message.line && file.endLine >= message.line);
                    const errorLineInFile = message.line - errorFile.startLine + 1;
                    message.errorLineInFile = errorLineInFile;
                    // console.error(`Lang: ${dictFilepath} File: ${errorFile.filePath} Line: ${errorLineInFile} JavaScript syntax error:`, message);
                    errors.push({
                        dist,
                        file: errorFile.filePath,
                        line: errorLineInFile,
                        message: message.message,
                        info: message
                    });
                });
            }
        }).catch(error => {
            console.error('JavaScript syntax error:', error);
        });
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
