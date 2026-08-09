import * as path from 'path';
import * as vscode from 'vscode';
import * as project from './project';
import * as toolchain from './toolchain';

export interface AzForgeTaskDefinition extends vscode.TaskDefinition
{
    type: 'azforge';
    target: 'Build' | 'Rebuild';
    configuration?: 'Debug' | 'Release';
    project?: string;
}

export function configuration(): 'Debug' | 'Release'
{
    return vscode.workspace.getConfiguration('azforge').get<'Debug' | 'Release'>('configuration') ?? 'Debug';
}

// Win32 is not a choice: the mod is loaded into game.exe, a 32-bit process.
async function make(mod: project.ModProject, target: 'Build' | 'Rebuild', config: 'Debug' | 'Release'): Promise<vscode.Task | undefined>
{
    const msbuild = await toolchain.msbuild();
    if (!msbuild)
    {
        return undefined;
    }

    const definition: AzForgeTaskDefinition = { type: 'azforge', target, configuration: config, project: mod.file };

    const args = [
        mod.file,
        '/nologo',
        '/v:minimal',
        `/t:${target}`,
        `/p:Configuration=${config}`,
        '/p:Platform=Win32'
    ];

    const folder = vscode.workspace.getWorkspaceFolder(vscode.Uri.file(mod.file)) ?? vscode.TaskScope.Workspace;

    // ProcessExecution rather than a shell: the paths carry spaces and PowerShell quoting has
    // already cost this project an afternoon on the Visual Studio side.
    const task = new vscode.Task(
        definition,
        folder,
        `${target.toLowerCase()} ${path.basename(mod.file, '.vcxproj')} (${config})`,
        'azforge',
        new vscode.ProcessExecution(msbuild, args, { cwd: mod.dir }),
        ['$msCompile']);

    task.group = target === 'Rebuild' ? vscode.TaskGroup.Rebuild : vscode.TaskGroup.Build;
    task.presentationOptions = { reveal: vscode.TaskRevealKind.Always, panel: vscode.TaskPanelKind.Dedicated, clear: true };

    return task;
}

// Resolves to the exit code, so Run can decline to launch a game that would load the old DLL
export async function run(task: vscode.Task): Promise<number>
{
    const execution = await vscode.tasks.executeTask(task);

    return new Promise<number>(resolve =>
    {
        const done = vscode.tasks.onDidEndTaskProcess(event =>
        {
            if (event.execution === execution)
            {
                done.dispose();
                resolve(event.exitCode ?? 1);
            }
        });
    });
}

export async function build(mod: project.ModProject, target: 'Build' | 'Rebuild', config = configuration()): Promise<number>
{
    const task = await make(mod, target, config);
    if (!task)
    {
        return 1;
    }

    return run(task);
}

export class Provider implements vscode.TaskProvider
{
    public static readonly type = 'azforge';

    public async provideTasks(): Promise<vscode.Task[]>
    {
        const tasks: vscode.Task[] = [];

        for (const file of await project.findAll())
        {
            const mod = project.load(file);

            for (const target of ['Build', 'Rebuild'] as const)
            {
                const task = await make(mod, target, configuration());
                if (task)
                {
                    tasks.push(task);
                }
            }
        }

        return tasks;
    }

    public async resolveTask(task: vscode.Task): Promise<vscode.Task | undefined>
    {
        const definition = task.definition as AzForgeTaskDefinition;

        const file = definition.project ?? (await project.findAll())[0];
        if (!file)
        {
            return undefined;
        }

        return make(project.load(file), definition.target ?? 'Build', definition.configuration ?? configuration());
    }
}
