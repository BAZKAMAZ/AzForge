import * as crypto from 'crypto';
import * as fs from 'fs';
import * as os from 'os';
import * as path from 'path';
import * as vscode from 'vscode';
import * as intellisense from './intellisense';

// AzForge.Mod.props is plain MSBuild and is shared with the Visual Studio side. It is looked for
// rather than named so that moving it out of plugins\vs2026 does not break this.
export function findProps(root: string): string | undefined
{
    const plugins = path.join(root, 'plugins');

    let entries: fs.Dirent[];
    try
    {
        entries = fs.readdirSync(plugins, { withFileTypes: true });
    }
    catch
    {
        return undefined;
    }

    for (const entry of entries)
    {
        if (!entry.isDirectory())
        {
            continue;
        }

        const candidate = path.join(plugins, entry.name, 'AzForge.Mod.props');
        if (fs.existsSync(candidate))
        {
            return candidate;
        }
    }

    return undefined;
}

function complaintAboutRoot(root: string): string | undefined
{
    if (!fs.existsSync(path.join(root, 'sdk', 'include', 'ForgeApi.h')))
    {
        return 'That folder has no sdk\\include\\ForgeApi.h, so a mod could not include the interface.';
    }

    if (!findProps(root))
    {
        return 'That folder has no plugins\\*\\AzForge.Mod.props, so a mod could not be built.';
    }

    return undefined;
}

// Picking sdk\, plugins\ or the folder AzForge itself sits in is the obvious mistake, so it is
// corrected rather than refused: the answer is one step up or one step down from what was chosen.
export function resolveRoot(picked: string): string | undefined
{
    if (!complaintAboutRoot(picked))
    {
        return picked;
    }

    let above = picked;
    for (let step = 0; step < 3; step += 1)
    {
        const parent = path.dirname(above);
        if (parent === above)
        {
            break;
        }

        above = parent;
        if (!complaintAboutRoot(above))
        {
            return above;
        }
    }

    let children: fs.Dirent[];
    try
    {
        children = fs.readdirSync(picked, { withFileTypes: true });
    }
    catch
    {
        return undefined;
    }

    for (const child of children)
    {
        if (child.isDirectory() && !complaintAboutRoot(path.join(picked, child.name)))
        {
            return path.join(picked, child.name);
        }
    }

    return undefined;
}

async function askFolder(title: string, current: string): Promise<string | undefined>
{
    const picked = await vscode.window.showOpenDialog({
        title,
        canSelectFolders: true,
        canSelectFiles: false,
        canSelectMany: false,
        defaultUri: current ? vscode.Uri.file(current) : undefined,
        openLabel: 'Select'
    });

    return picked?.[0]?.fsPath;
}

function fill(template: string, values: Record<string, string>): string
{
    let text = template;

    for (const [token, value] of Object.entries(values))
    {
        text = text.split(`$${token}$`).join(value);
    }

    return text;
}

function escapeXml(text: string): string
{
    return text.replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');
}

export async function create(context: vscode.ExtensionContext): Promise<void>
{
    const config = vscode.workspace.getConfiguration('azforge');

    let root = config.get<string>('azforgeRoot') ?? '';

    // Asked again rather than given up on: a wrong folder is a slip, not a reason to make the
    // whole wizard be started over.
    while (complaintAboutRoot(root))
    {
        const picked = await askFolder('The AzForge folder - the one holding sdk\\ and plugins\\', root);
        if (!picked)
        {
            return;
        }

        const resolved = resolveRoot(picked);
        if (resolved)
        {
            root = resolved;
            break;
        }

        const answer = await vscode.window.showErrorMessage(
            `'${picked}' is not the AzForge folder: nothing in it or beside it has both sdk\\include\\ForgeApi.h and plugins\\*\\AzForge.Mod.props.`,
            { modal: true },
            'Pick again');

        if (answer !== 'Pick again')
        {
            return;
        }

        root = '';
    }

    const props = findProps(root);
    if (!props)
    {
        vscode.window.showErrorMessage(`'${root}' has no plugins\\*\\AzForge.Mod.props.`);
        return;
    }

    const name = await vscode.window.showInputBox({
        title: 'New AzForge mod (1 of 5)',
        prompt: 'What the mod is called on the MODS screen',
        value: 'MyMod',
        validateInput: value => value.trim().length === 0 ? 'A mod needs a name.' : undefined
    });

    if (!name)
    {
        return;
    }

    const id = await vscode.window.showInputBox({
        title: 'New AzForge mod (2 of 5)',
        prompt: 'Names the mod everywhere. Two mods sharing one id are the same mod.',
        value: 'azforge.' + name.trim().toLowerCase().replace(/[^a-z0-9]+/g, ''),
        validateInput: value => /^[a-z0-9][a-z0-9._-]*$/.test(value.trim())
            ? undefined
            : 'Lower case letters, digits, dot, dash and underscore.'
    });

    if (!id)
    {
        return;
    }

    const description = await vscode.window.showInputBox({
        title: 'New AzForge mod (3 of 5)',
        prompt: 'One line, shown beside the name. Leave empty to skip.'
    });

    if (description === undefined)
    {
        return;
    }

    const author = await vscode.window.showInputBox({
        title: 'New AzForge mod (4 of 5)',
        prompt: 'Author',
        value: os.userInfo().username
    });

    if (author === undefined)
    {
        return;
    }

    let game = config.get<string>('gameDir') ?? '';
    const pickedGame = await askFolder('The game folder - the one holding game.exe (5 of 5)', game);
    if (!pickedGame)
    {
        return;
    }
    game = pickedGame.replace(/\\+$/, '');

    // Worth saying, not worth refusing over: the mod still builds and packs, it just goes nowhere
    if (!fs.existsSync(path.join(game, 'game.exe')))
    {
        const answer = await vscode.window.showWarningMessage(
            'There is no game.exe in that folder. The mod will build and pack, but it will not be installed anywhere and there will be nothing to run.',
            { modal: true },
            'Create anyway');

        if (answer !== 'Create anyway')
        {
            return;
        }
    }

    const parent = await askFolder('Where to put the new mod', path.dirname(root));
    if (!parent)
    {
        return;
    }

    const safe = name.trim().replace(/[^A-Za-z0-9_]/g, '');
    const dir = path.join(parent, safe);

    if (fs.existsSync(dir) && fs.readdirSync(dir).length > 0)
    {
        vscode.window.showErrorMessage(`${dir} already has something in it.`);
        return;
    }

    const values = {
        safeprojectname: safe,
        guid1: crypto.randomUUID().toUpperCase(),
        modid: escapeXml(id.trim()),
        modname: escapeXml(name.trim()),
        modauthor: escapeXml(author.trim()),
        moddescription: escapeXml(description.trim()),
        gamedir: escapeXml(game),
        azforgedir: escapeXml(root.replace(/\\+$/, '')),
        propspath: escapeXml(props)
    };

    const templates = path.join(context.extensionPath, 'templates');
    const read = (file: string) => fs.readFileSync(path.join(templates, file), 'utf8');

    fs.mkdirSync(path.join(dir, 'resmod'), { recursive: true });
    fs.writeFileSync(path.join(dir, `${safe}.vcxproj`), fill(read('Mod.vcxproj'), values), 'utf8');
    fs.writeFileSync(path.join(dir, 'Mod.cpp'), read('Mod.cpp'), 'utf8');
    fs.writeFileSync(path.join(dir, 'resmod', 'README.txt'), read('resmod_readme.txt'), 'utf8');
    fs.writeFileSync(path.join(dir, '.gitignore'), read('gitignore'), 'utf8');
    intellisense.write(dir, path.join(root, 'sdk'));

    await config.update('azforgeRoot', root, vscode.ConfigurationTarget.Global);
    await config.update('gameDir', game, vscode.ConfigurationTarget.Global);

    await vscode.commands.executeCommand('vscode.openFolder', vscode.Uri.file(dir), { forceNewWindow: vscode.workspace.workspaceFolders !== undefined });
}
