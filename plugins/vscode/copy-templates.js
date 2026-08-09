// The mod template is shared with the Visual Studio side and lives in plugins\templates. It is
// copied in rather than kept here as well, because two copies of a project template drift. The
// copy is a build output and is not committed.
const fs = require('fs');
const path = require('path');

const from = path.join(__dirname, '..', 'templates');
const to = path.join(__dirname, 'templates');

if (!fs.existsSync(path.join(from, 'Mod.vcxproj')))
{
    console.error(`The shared template is not at ${from}`);
    process.exit(1);
}

fs.rmSync(to, { recursive: true, force: true });
fs.cpSync(from, to, { recursive: true });

// README.md explains the template to whoever edits it, and has no business shipping in the VSIX
fs.rmSync(path.join(to, 'README.md'), { force: true });

console.log(`templates <- ${from}`);
