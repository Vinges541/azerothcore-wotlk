'use strict';

// Entry point injected into an isolated wow.export 0.2.19 bundle.
const fs = require('fs').promises;
const path = require('path');

async function run({ core, log, CASCRemote, ADTExporter }) {
  const output = process.env.WXL_MAP_EXPORT_ROOT;
  if (!output || !path.isAbsolute(output))
    throw new Error('WXL_MAP_EXPORT_ROOT must be an absolute output directory');

  const status = { state: 'starting', map: 'Azeroth', mapId: 0, tile: '32_48', failures: [] };
  const save = async () => {
    await fs.mkdir(output, { recursive: true });
    await fs.writeFile(path.join(output, 'export-status.json'), JSON.stringify(status, null, 2) + '\n');
  };
  try {
    await save();
    Object.assign(core.view.config, {
      exportDirectory: path.join(output, 'raw'), exportMapFormat: 'RAW',
      enableSharedChildren: true, enableSharedTextures: true,
      modelsExportTextures: true, modelsExportSkin: true, modelsExportSkel: true,
      modelsExportBone: true, modelsExportAnim: true, modelsExportWMOGroups: true,
      mapsIncludeWMO: true, mapsIncludeM2: true, mapsIncludeWMOSets: true,
      mapsIncludeFoliage: false, mapsIncludeGameObjects: false, mapsIncludeLiquid: false,
      overwriteFiles: false, removePathSpaces: false, pathFormat: 'posix',
    });
    const source = new CASCRemote('eu');
    await source.init();
    const index = source.builds.findIndex(build => build.Product === 'wow');
    if (index < 0) throw new Error('Retail source unavailable');
    const build = source.builds[index];
    status.build = {
      version: build.VersionsName, buildConfig: build.BuildConfig, cdnConfig: build.CDNConfig,
    };
    status.state = 'loading-casc';
    await save();
    await source.load(index);
    core.view.casc = source;
    const getFile = source.getFile.bind(source);
    source.getFile = async (...args) => {
      try { return await getFile(...args); }
      catch (error) {
        status.failures.push({ fileDataId: args[0], error: String(error) });
        await save();
        throw error;
      }
    };
    const helper = {
      isCancelled: () => false,
      setCurrentTaskName: name => { log.write('[map-probe] %s', name); },
      setCurrentTaskMax: () => {}, setCurrentTaskValue: () => {},
    };
    status.state = 'exporting';
    await save();
    const directory = path.join(output, 'raw', 'maps', status.map);
    await fs.mkdir(directory, { recursive: true });
    // wow.export reverses index axes when constructing its terrain filename.
    const exporter = new ADTExporter(status.mapId, status.map, 32 * 64 + 48);
    await exporter.export(directory, 0, undefined, helper);
    status.state = status.failures.length ? 'exported-with-fetch-errors' : 'exported-needs-audit';
    await save();
  } catch (error) {
    status.state = 'failed';
    status.error = error.stack || String(error);
    await save();
    log.write('[map-probe] failed: %s', status.error);
  }
}

module.exports = { run };
