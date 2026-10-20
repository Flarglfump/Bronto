#!/usr/bin/env python3
"""Packs the BrontoScript extension into a .vsix (a zip VS Code installs).

    python3 tools/vscode/package_vsix.py [output folder]     (default: build)

Uses only Python's standard library. Install the result in VS Code with
"Extensions: Install from VSIX...", or `code --install-extension <file>`.
"""
import json, os, sys, zipfile
from xml.sax.saxutils import escape

here = os.path.dirname(os.path.abspath(__file__))
src = os.path.join(here, 'brontoscript')
out_dir = sys.argv[1] if len(sys.argv) > 1 else 'build'
pkg = json.load(open(os.path.join(src, 'package.json'), encoding='utf-8'))
files = ['package.json', 'extension.js', 'README.md', 'language-configuration.json',
         'lib/language.js', 'lib/builtins.json', 'syntaxes/brontoscript.tmLanguage.json', 'snippets/brontoscript.json',
         'images/icon.png']

content_types = '''<?xml version="1.0" encoding="utf-8"?>
<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">
<Default Extension=".json" ContentType="application/json"/><Default Extension=".js" ContentType="application/javascript"/>
<Default Extension=".md" ContentType="text/markdown"/><Default Extension=".png" ContentType="image/png"/><Default Extension=".vsixmanifest" ContentType="text/xml"/>
</Types>'''

manifest = f'''<?xml version="1.0" encoding="utf-8"?>
<PackageManifest Version="2.0.0" xmlns="http://schemas.microsoft.com/developer/vsx-schema/2011" xmlns:d="http://schemas.microsoft.com/developer/vsx-schema-design/2011">
  <Metadata>
    <Identity Language="en-US" Id="{pkg['name']}" Version="{pkg['version']}" Publisher="{pkg['publisher']}" />
    <DisplayName>{escape(pkg['displayName'])}</DisplayName>
    <Description xml:space="preserve">{escape(pkg['description'])}</Description>
    <Tags>{escape(','.join(pkg.get('keywords', [])))}</Tags>
    <Categories>{escape(','.join(pkg['categories']))}</Categories>
    <GalleryFlags>Public</GalleryFlags>
    <Properties>
      <Property Id="Microsoft.VisualStudio.Code.Engine" Value="{escape(pkg['engines']['vscode'])}" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionDependencies" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionPack" Value="" />
      <Property Id="Microsoft.VisualStudio.Code.ExtensionKind" Value="workspace" />
      <Property Id="Microsoft.VisualStudio.Code.LocalizedLanguages" Value="" />
    </Properties>
  </Metadata>
  <Installation><InstallationTarget Id="Microsoft.VisualStudio.Code" /></Installation>
  <Dependencies />
  <Assets>
    <Asset Type="Microsoft.VisualStudio.Code.Manifest" Path="extension/package.json" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Content.Details" Path="extension/README.md" Addressable="true" />
    <Asset Type="Microsoft.VisualStudio.Services.Icons.Default" Path="extension/images/icon.png" Addressable="true" />
  </Assets>
</PackageManifest>'''

os.makedirs(out_dir, exist_ok=True)
target = os.path.join(out_dir, f"{pkg['name']}-{pkg['version']}.vsix")
with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as z:
    z.writestr('[Content_Types].xml', content_types)
    z.writestr('extension.vsixmanifest', manifest)
    for f in files:
        z.write(os.path.join(src, f), 'extension/' + f)
print('wrote', target)
