import fs from 'fs';
import {ESLint} from 'eslint';
import path from 'path';
import * as core from '@actions/core';
import {Octokit} from "octokit";

const githubToken = process.env.GITHUB_TOKEN;
const branch = process.env.BRANCH_NAME;

// Read the dictionary file and parse it into a JavaScript object
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

// Replace ej content in the file
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

// Recursively read all files in the directory
function readDirectoryRecursive(directory, ignoreFolders = []) {
    let files = [];
    const items = fs.readdirSync(directory);
    items.forEach(item => {
        const itemPath = path.join(directory, item);
        const stats = fs.lstatSync(itemPath);
        if (!ignoreFolders.some(ignorePath => itemPath.includes(ignorePath))) {
            if (!stats.isSymbolicLink()) {
                if (stats.isFile()) {
                    files.push(itemPath);
                } else if (stats.isDirectory()) {
                    const subFiles = readDirectoryRecursive(itemPath, ignoreFolders);
                    files = files.concat(subFiles);
                }
            }
        }
    });
    return files;
}

async function main() {

    const errors = [];
    const directory = './';

    const octokit = new Octokit({auth: githubToken});
    const owner = process.env?.GITHUB_REPOSITORY_OWNER || "";
    const repo = process.env?.GITHUB_REPOSITORY?.split('/')[1] || "";

    const eslint = new ESLint({
        overrideConfigFile: 'eslint.config.mjs'
    });

    const dists = ['BR', 'CN', 'CZ', 'DA', 'DE', 'EN', 'ES', 'FI', 'FR', 'HU', 'IT', 'JP', 'KR', 'MS', 'NL', 'NO', 'PL', 'RO', 'RU', 'SL', 'SV', 'TH', 'TR', 'TW', 'UK'];
    // const dists = ['EN'];

    const ignoreFolders = ['node_modules','charts'];
    const ignoreFiles = ['jquery-ui.js', 'loader.js', 'jquery.js', 'jquery.min.js', 'jquery-ui.min.js', 'chart.js', 'jscolor.js', 'jquery.mobile.js'];

    for (const dist of dists) {
        const dictFilepath = path.join(directory, `${dist}.dict`);
        const dictionary = parseDictionary(dictFilepath);
        console.log(`> Dist: ${dist}`);

        const files = readDirectoryRecursive(directory, ignoreFolders);
        // const files = fs.readdirSync(directory);
        for (const file of files) {
            if (path.extname(file) === '.js' && !ignoreFiles.includes(path.basename(file)) && !path.basename(file).includes('.min.js')) {
                const filepath = path.join(directory, file);
                const originalContent = fs.readFileSync(filepath, 'utf8');
                const replacedContent = replaceContent(originalContent, dictionary);

                // Perform a syntax check on the replaced content
                await eslint.lintText(replacedContent, {filePath: filepath}).then(([result]) => {
                    if (!(result.errorCount === 0 && result.warningCount === 0)) {
                        console.error(`Lang: ${dictFilepath} File: ${filepath} JavaScript syntax error:`, result.messages);
                        errors.push({dist, file, messages: result.messages});
                    }
                }).catch(error => {
                    console.error('JavaScript syntax error:', error);
                });
            }

        }
    }

    if (errors.length > 0) {

        await octokit.request('POST /repos/{owner}/{repo}/issues', {
            owner: owner,
            repo: repo,
            title: `[Bot] Found JS Syntax error (${branch})`,
            body: `Branch: ${branch}\n` + errors.map(error => `Dist: ${error.dist}, File: ${error.file}, Line: ${error.messages.map(message => message.line)}, Column: ${error.messages.map(message => message.column)}, Message: ${error.messages.map(message => message.message).join(', ')}`).join('\n'),
            labels: [
                'syntax'
            ],
            headers: {
                'X-GitHub-Api-Version': '2022-11-28'
            }
        })

        throw errors;
    }
}

main()
    .then(() => {
        console.log('All JavaScript syntax check pass!');
    })
    .catch(error => {
        // console.error('Some error occurred:', error);
        core.setFailed(error);
    });
