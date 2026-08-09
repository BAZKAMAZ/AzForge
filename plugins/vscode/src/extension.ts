import * as fs from 'fs';
import * as path from 'path';
import * as vscode from 'vscode';
import * as intellisense from './intellisense';
import * as newMod from './newMod';
import * as project from './project';
import * as settingsView from './settingsView';
import * as tasks from './tasks';
import * as toolchain from './toolchain';

let status: vscode.StatusBarItem;

async function refreshStatus(): Promise<void>
{
    const mods = await project.findAll();
    vscode.commands.executeCommand('setContext', 'azforge.hasMod', mods.length > 0);

    if (mods.length === 0)
    {
        status.hide();
        return;
    }

    const name = mods.length === 1
        ? (project.load(mods[0]).settings['AzForgeModName'] || path.basename(mods[0], '.vcxproj'))
        : `${mods.length} mods`;

    status.text = `$(tools) ${name}`;
    status.tooltip = `Build the mod (${tasks.configuration()}), pack it and install it into the game`;
    status.command = 'azforge.build';
    status.show();
}

async function build(target: 'Build' | 'Rebuild'): Promise<boolean>
{
    const mod = await project.pick();
    if (!mod)
    {
        return false;
    }

    return await tasks.build(mod, target) === 0;
}

async function pack(): Promise<void>
{
    const mod = await project.pick();
    if (!mod)
    {
        return;
    }

    if (await tasks.build(mod, 'Build', 'Release') !== 0)
    {
        return;
    }

    const amod = path.join(mod.dir, 'Release', `${path.basename(mod.file, '.vcxproj')}.amod`);
    if (!fs.existsSync(amod))
    {
        vscode.window.showWarningMessage(`The build reported success but ${amod} is not there.`);
        return;
    }

    const answer = await vscode.window.showInformationMessage(`Packed ${path.basename(amod)}.`, 'Show in Explorer');
    if (answer)
    {
        vscode.commands.executeCommand('revealFileInOS', vscode.Uri.file(amod));
    }
}

async function run(): Promise<void>
{
    const mod = await project.pick();
    if (!mod)
    {
        return;
    }

    const dir = project.gameDir(mod);
    const exe = dir ? path.join(dir, 'game.exe') : '';

    if (!exe || !fs.existsSync(exe))
    {
        const answer = await vscode.window.showErrorMessage(
            'There is no game.exe to run. Set the game folder on the mod.',
            'Mod Settings');

        if (answer)
        {
            vscode.commands.executeCommand('azforge.modSettings');
        }
        return;
    }

    // The mod is a copy taken out of the .amod, so a stale build would be loaded silently
    if (await tasks.build(mod, 'Build') !== 0)
    {
        return;
    }

    if (!vscode.extensions.getExtension('ms-vscode.cpptools'))
    {
        vscode.window.showErrorMessage('Debugging a mod needs the C/C++ extension (ms-vscode.cpptools), which is what provides cppvsdbg.');
        return;
    }

    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(mod.file));

    // Built from the .vcxproj rather than kept in a launch.json, so the game folder stays in one place
    const started = await vscode.debug.startDebugging(folder, {
        type: 'cppvsdbg',
        request: 'launch',
        name: `${mod.settings['AzForgeModName'] || 'AzForge'} in the game`,
        program: exe,
        cwd: dir,
        console: 'internalConsole',
        symbolSearchPath: path.join(mod.dir, tasks.configuration())
    });

    if (!started)
    {
        vscode.window.showErrorMessage('The debugger did not start.');
    }
}

async function openLog(): Promise<void>
{
    const mod = await project.pick();
    if (!mod)
    {
        return;
    }

    const log = path.join(project.gameDir(mod), 'forge.log');
    if (!fs.existsSync(log))
    {
        vscode.window.showWarningMessage(`There is no forge.log in the game folder yet. It is written when the game runs.`);
        return;
    }

    await vscode.window.showTextDocument(await vscode.workspace.openTextDocument(vscode.Uri.file(log)));
}

async function refreshIntelliSense(): Promise<void>
{
    const mod = await project.pick();
    if (!mod)
    {
        return;
    }

    const sdk = project.sdkDir(mod);
    if (!fs.existsSync(path.join(sdk, 'include', 'ForgeApi.h')))
    {
        vscode.window.showErrorMessage(`The SDK folder on this mod is '${sdk}', which has no include\\ForgeApi.h.`);
        return;
    }

    vscode.window.showInformationMessage(`IntelliSense now looks in ${path.join(sdk, 'include')}. Written to ${intellisense.refresh(mod)}.`);
}

export function activate(context: vscode.ExtensionContext): void
{
    status = vscode.window.createStatusBarItem(vscode.StatusBarAlignment.Left, 40);
    context.subscriptions.push(status);

    context.subscriptions.push(
        vscode.commands.registerCommand('azforge.newMod', () => newMod.create(context)),
        vscode.commands.registerCommand('azforge.build', () => build('Build')),
        vscode.commands.registerCommand('azforge.rebuild', () => build('Rebuild')),
        vscode.commands.registerCommand('azforge.pack', () => pack()),
        vscode.commands.registerCommand('azforge.run', () => run()),
        vscode.commands.registerCommand('azforge.modSettings', () => settingsView.show(context)),
        vscode.commands.registerCommand('azforge.refreshIntelliSense', () => refreshIntelliSense()),
        vscode.commands.registerCommand('azforge.openLog', () => openLog()));

    context.subscriptions.push(vscode.tasks.registerTaskProvider(tasks.Provider.type, new tasks.Provider()));

    // A .vcxproj added, removed or edited changes what there is to build and what it is called
    const watcher = vscode.workspace.createFileSystemWatcher('**/*.vcxproj');
    const changed = () => { project.forget(); refreshStatus(); };
    watcher.onDidCreate(changed);
    watcher.onDidDelete(changed);
    watcher.onDidChange(refreshStatus);
    context.subscriptions.push(watcher);

    context.subscriptions.push(vscode.workspace.onDidChangeConfiguration(event =>
    {
        if (event.affectsConfiguration('azforge.msbuildPath'))
        {
            toolchain.forget();
        }
        if (event.affectsConfiguration('azforge'))
        {
            refreshStatus();
        }
    }));

    refreshStatus();
}

export function deactivate(): void
{
}
