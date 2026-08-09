import * as crypto from 'crypto';
import * as vscode from 'vscode';
import * as project from './project';

interface Field
{
    key: project.SettingKey;
    label: string;
    hint?: string;
    browse?: 'file' | 'folder';
}

const GROUPS: { title: string; fields: Field[] }[] = [
    {
        title: 'Mod',
        fields: [
            { key: 'AzForgeModName', label: 'Name', hint: 'Shown on the MODS screen.' },
            { key: 'AzForgeModId', label: 'Id', hint: 'Names the mod everywhere. Two mods sharing one are the same mod.' },
            { key: 'AzForgeModVersion', label: 'Version', hint: "The mod's own version, not the API's." },
            { key: 'AzForgeModAuthor', label: 'Author' },
            { key: 'AzForgeModDescription', label: 'Description', hint: 'One line, shown beside the name.' },
            { key: 'AzForgeModIcon', label: 'Icon', hint: 'A .bmp shown on the MODS screen.', browse: 'file' }
        ]
    },
    {
        title: 'Game',
        fields: [
            { key: 'AzForgeGameDir', label: 'Game folder', hint: 'Holds game.exe. The mod is installed into its mods folder, and Run starts it.', browse: 'folder' },
            { key: 'AzForgeResourceDir', label: 'Resource folder', hint: 'Everything in here is shipped, laid out the way the game asks for it. Defaults to resmod beside the project.', browse: 'folder' }
        ]
    },
    {
        title: 'Tools',
        fields: [
            { key: 'AzForgeSdkDir', label: 'SDK folder', hint: 'Holds include\\ForgeApi.h. api_version is read from it.', browse: 'folder' },
            { key: 'AmodPackPath', label: 'amodpack', hint: 'Seals the package into a .amod and installs it.', browse: 'file' }
        ]
    }
];

let panel: vscode.WebviewPanel | undefined;

function escapeHtml(text: string): string
{
    return text
        .replace(/&/g, '&amp;')
        .replace(/</g, '&lt;')
        .replace(/>/g, '&gt;')
        .replace(/"/g, '&quot;');
}

function body(mod: project.ModProject, known: string[]): string
{
    const required = new Set((mod.settings['AzForgeModRequires'] ?? '')
        .split(';')
        .map(one => one.trim())
        .filter(one => one.length > 0));

    const groups = GROUPS.map(group =>
    {
        const rows = group.fields.map(field =>
        {
            const value = escapeHtml(mod.settings[field.key] ?? '');
            const browse = field.browse
                ? `<button class="browse" data-key="${field.key}" data-kind="${field.browse}">...</button>`
                : '';

            return `<div class="row">
                <label for="${field.key}">${field.label}</label>
                <div class="control">
                    <div class="input"><input id="${field.key}" data-key="${field.key}" value="${value}" />${browse}</div>
                    ${field.hint ? `<p class="hint">${escapeHtml(field.hint)}</p>` : ''}
                </div>
            </div>`;
        }).join('');

        return `<section><h2>${group.title}</h2>${rows}</section>`;
    }).join('');

    // Empty means the build works the list out from what the sources use, and keeps working it out
    const boxes = known.length > 0
        ? known.map(one => `<label class="box"><input type="checkbox" data-subsystem="${one}" ${required.has(one) ? 'checked' : ''} /> ${one}</label>`).join('')
        : `<p class="hint">sdk\\subsystems.txt was not found beside the SDK folder, so the list cannot be shown. It is written by AzForge's own build.</p>`;

    return `${groups}
    <section>
        <h2>Requires</h2>
        <p class="hint">Subsystems the mod cannot work without. The loader refuses to load the mod rather than hand it a table that is not there.</p>
        <p class="hint">Tick nothing and the build works the list out from what the sources actually use, every build. Tick something and that list is used as written - which is what a mod wants when it uses something only if it happens to be there.</p>
        <div class="boxes">${boxes}</div>
    </section>
    <div class="actions">
        <button id="save" class="primary">Save</button>
        <span id="status"></span>
    </div>`;
}

function html(mod: project.ModProject, known: string[]): string
{
    const nonce = crypto.randomBytes(16).toString('base64');

    return `<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8" />
<meta http-equiv="Content-Security-Policy" content="default-src 'none'; style-src 'unsafe-inline'; script-src 'nonce-${nonce}';" />
<style>
    body { font-family: var(--vscode-font-family); font-size: var(--vscode-font-size); color: var(--vscode-foreground); padding: 12px 18px 32px; }
    h1 { font-size: 1.3em; font-weight: 600; margin: 0 0 2px; }
    h2 { font-size: 1em; font-weight: 600; margin: 22px 0 8px; padding-bottom: 4px; border-bottom: 1px solid var(--vscode-panel-border); }
    .file { color: var(--vscode-descriptionForeground); margin: 0 0 8px; font-size: 0.9em; }
    .row { display: flex; gap: 12px; margin-bottom: 10px; align-items: baseline; }
    .row label { flex: 0 0 128px; text-align: right; padding-top: 4px; }
    .control { flex: 1 1 auto; min-width: 0; }
    .input { display: flex; gap: 6px; }
    input[type=text], input:not([type]) { flex: 1 1 auto; min-width: 0; padding: 4px 6px; color: var(--vscode-input-foreground); background: var(--vscode-input-background); border: 1px solid var(--vscode-input-border, transparent); border-radius: 2px; }
    input:focus { outline: 1px solid var(--vscode-focusBorder); }
    .hint { color: var(--vscode-descriptionForeground); font-size: 0.9em; margin: 3px 0 0; }
    button { padding: 4px 12px; color: var(--vscode-button-secondaryForeground); background: var(--vscode-button-secondaryBackground); border: none; border-radius: 2px; cursor: pointer; }
    button.primary { color: var(--vscode-button-foreground); background: var(--vscode-button-background); padding: 6px 18px; }
    button:hover { background: var(--vscode-button-secondaryHoverBackground); }
    button.primary:hover { background: var(--vscode-button-hoverBackground); }
    .boxes { display: flex; flex-wrap: wrap; gap: 6px 20px; margin-top: 10px; }
    .box { display: flex; align-items: center; gap: 6px; flex: 0 0 auto; }
    .actions { margin-top: 26px; display: flex; align-items: center; gap: 14px; }
    #status { color: var(--vscode-descriptionForeground); }
</style>
</head>
<body>
<h1>Mod Settings</h1>
<p class="file">${escapeHtml(mod.file)} &mdash; mod.json is written from these at every build.</p>
${body(mod, known)}
<script nonce="${nonce}">
    const vscode = acquireVsCodeApi();

    function collect()
    {
        const values = {};
        document.querySelectorAll('input[data-key]').forEach(input => values[input.dataset.key] = input.value.trim());

        const ticked = [];
        document.querySelectorAll('input[data-subsystem]').forEach(box => { if (box.checked) ticked.push(box.dataset.subsystem); });
        values.AzForgeModRequires = ticked.join(';');

        return values;
    }

    document.querySelectorAll('.browse').forEach(button =>
    {
        button.addEventListener('click', () => vscode.postMessage({ what: 'browse', key: button.dataset.key, kind: button.dataset.kind }));
    });

    document.getElementById('save').addEventListener('click', () =>
    {
        vscode.postMessage({ what: 'save', values: collect() });
    });

    window.addEventListener('message', event =>
    {
        const message = event.data;

        if (message.what === 'picked')
        {
            document.querySelector('input[data-key="' + message.key + '"]').value = message.path;
        }
        else if (message.what === 'saved')
        {
            const status = document.getElementById('status');
            status.textContent = 'Saved.';
            setTimeout(() => status.textContent = '', 2500);
        }
    });
</script>
</body>
</html>`;
}

export async function show(context: vscode.ExtensionContext): Promise<void>
{
    const mod = await project.pick();
    if (!mod)
    {
        return;
    }

    if (!panel)
    {
        panel = vscode.window.createWebviewPanel('azforge.modSettings', 'Mod Settings', vscode.ViewColumn.Active, { enableScripts: true, retainContextWhenHidden: true });
        panel.onDidDispose(() => panel = undefined, undefined, context.subscriptions);

        panel.webview.onDidReceiveMessage(async message =>
        {
            if (message.what === 'browse')
            {
                const picked = await vscode.window.showOpenDialog({
                    canSelectFolders: message.kind === 'folder',
                    canSelectFiles: message.kind === 'file',
                    canSelectMany: false,
                    defaultUri: vscode.Uri.file(mod.dir),
                    openLabel: 'Select'
                });

                if (picked?.[0])
                {
                    panel?.webview.postMessage({ what: 'picked', key: message.key, path: picked[0].fsPath });
                }
            }
            else if (message.what === 'save')
            {
                try
                {
                    project.write(mod.file, message.values);
                    panel?.webview.postMessage({ what: 'saved' });
                }
                catch (error)
                {
                    vscode.window.showErrorMessage(`Could not write the settings: ${error instanceof Error ? error.message : error}`);
                }
            }
        }, undefined, context.subscriptions);
    }

    panel.webview.html = html(mod, project.subsystems(mod));
    panel.reveal();
}
