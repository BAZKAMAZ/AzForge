import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';

// The same fields the Visual Studio side keeps on its Mod Settings page. They live in the
// .vcxproj because mod.json is written from them at build time, and a second place to keep the
// same facts is a second place for them to be wrong.
export const SETTING_KEYS = [
    'AzForgeModId',
    'AzForgeModName',
    'AzForgeModVersion',
    'AzForgeModAuthor',
    'AzForgeModDescription',
    'AzForgeModRequires',
    'AzForgeModIcon',
    'AzForgeGameDir',
    'AzForgeResourceDir',
    'AzForgeSdkDir',
    'AmodPackPath'
] as const;

export type SettingKey = (typeof SETTING_KEYS)[number];

export interface ModProject
{
    file: string;
    dir: string;
    settings: Record<string, string>;
}

const EXCLUDED = '**/{obj,bin,.vs,.git,node_modules,Debug,Release,x64,Win32}/**';

export function unescapeXml(text: string): string
{
    return text
        .replace(/&lt;/g, '<')
        .replace(/&gt;/g, '>')
        .replace(/&quot;/g, '"')
        .replace(/&apos;/g, "'")
        .replace(/&amp;/g, '&');
}

export function escapeXml(text: string): string
{
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;');
}

function readSettings(text: string): Record<string, string>
{
    const settings: Record<string, string> = {};

    for (const key of SETTING_KEYS)
    {
        // Last one wins: the .vcxproj may carry a per-configuration override after the Globals group
        const matches = [...text.matchAll(new RegExp(`<${key}\\s*>([\\s\\S]*?)</${key}>|<${key}\\s*/>`, 'g'))];
        if (matches.length > 0)
        {
            settings[key] = unescapeXml(matches[matches.length - 1][1] ?? '').trim();
        }
    }

    return settings;
}

export function load(file: string): ModProject
{
    const text = fs.readFileSync(file, 'utf8');
    return { file, dir: path.dirname(file), settings: readSettings(text) };
}

export function isModProject(text: string): boolean
{
    return text.includes('AzForge.Mod.props') || text.includes('<AzForgeModId');
}

export async function findAll(): Promise<string[]>
{
    const found = await vscode.workspace.findFiles('**/*.vcxproj', EXCLUDED, 64);
    const mods: string[] = [];

    for (const uri of found)
    {
        try
        {
            if (isModProject(fs.readFileSync(uri.fsPath, 'utf8')))
            {
                mods.push(uri.fsPath);
            }
        }
        catch
        {
            // A project that cannot be read is not a project we can build
        }
    }

    return mods.sort();
}

let remembered: string | undefined;

// Asks only when the workspace holds more than one mod, and only once per session
export async function pick(): Promise<ModProject | undefined>
{
    const mods = await findAll();

    if (mods.length === 0)
    {
        vscode.window.showErrorMessage('No AzForge mod project in this workspace. A mod project is a .vcxproj importing AzForge.Mod.props.');
        return undefined;
    }

    if (mods.length === 1)
    {
        return load(mods[0]);
    }

    if (remembered && mods.includes(remembered))
    {
        return load(remembered);
    }

    const choice = await vscode.window.showQuickPick(
        mods.map(file => ({ label: path.basename(file, '.vcxproj'), description: file })),
        { title: 'Which mod?' });

    if (!choice)
    {
        return undefined;
    }

    remembered = choice.description;
    return load(remembered);
}

export function forget(): void
{
    remembered = undefined;
}

// Rewrites the value in place rather than round-tripping the XML, which would reformat a file the
// author also opens in Visual Studio.
export function write(file: string, values: Partial<Record<SettingKey, string>>): void
{
    let text = fs.readFileSync(file, 'utf8');

    for (const [key, value] of Object.entries(values))
    {
        const escaped = escapeXml(value ?? '');
        const element = new RegExp(`<${key}\\s*>[\\s\\S]*?</${key}>|<${key}\\s*/>`);

        if (element.test(text))
        {
            text = text.replace(element, `<${key}>${escaped}</${key}>`);
            continue;
        }

        // Nothing to say and nowhere it is said: leave the project alone rather than add an empty element
        if (escaped.length === 0)
        {
            continue;
        }

        const globals = /(<PropertyGroup\s+Label="Globals"\s*>)/;
        if (globals.test(text))
        {
            text = text.replace(globals, `$1\n    <${key}>${escaped}</${key}>`);
        }
        else
        {
            throw new Error(`${path.basename(file)} has no <PropertyGroup Label="Globals">, so ${key} has nowhere to go.`);
        }
    }

    fs.writeFileSync(file, text, 'utf8');
}

export function gameDir(project: ModProject): string
{
    const own = project.settings['AzForgeGameDir'];
    if (own)
    {
        return own.replace(/\\+$/, '');
    }

    return (vscode.workspace.getConfiguration('azforge').get<string>('gameDir') ?? '').replace(/\\+$/, '');
}

// The .vcxproj says <AzForgeSdkDir>C:\...\AzForge\sdk\</AzForgeSdkDir>; the root is its parent
export function sdkDir(project: ModProject): string
{
    const own = project.settings['AzForgeSdkDir'];
    if (own)
    {
        return path.resolve(project.dir, own.replace(/\\+$/, ''));
    }

    const root = vscode.workspace.getConfiguration('azforge').get<string>('azforgeRoot') ?? '';
    return root ? path.join(root, 'sdk') : '';
}

// The subsystems this loader offers, as its own build recorded them
export function subsystems(project: ModProject): string[]
{
    const list = path.join(sdkDir(project), 'subsystems.txt');

    try
    {
        return fs.readFileSync(list, 'utf8')
            .split(/\r?\n/)
            .map(line => line.trim())
            .filter(line => line.length > 0);
    }
    catch
    {
        return [];
    }
}
