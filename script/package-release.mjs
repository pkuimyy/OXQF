import { createHash } from "node:crypto";
import {
  mkdir,
  readFile,
  readdir,
  rm,
  stat,
  writeFile,
} from "node:fs/promises";
import { spawnSync } from "node:child_process";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

const scriptDirectory = path.dirname(fileURLToPath(import.meta.url));
const projectRoot = path.resolve(scriptDirectory, "..");

export function projectVersion(cmakeSource) {
  const match = cmakeSource.match(
    /project\(\s*OXQF\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)/s,
  );
  if (!match) {
    throw new Error("cannot read OXQF version from CMakeLists.txt");
  }
  return match[1];
}

export function releasePlatform(platform = process.platform, architecture = process.arch) {
  const architectures = { x64: "x86_64", arm64: "arm64" };
  const arch = architectures[architecture];
  if (!arch) {
    throw new Error(`unsupported release architecture: ${architecture}`);
  }
  if (platform === "linux") {
    return { name: "linux", arch, compiler: "gcc", extension: "tar.gz" };
  }
  if (platform === "win32") {
    if (architecture !== "x64") {
      throw new Error(`unsupported Windows release architecture: ${architecture}`);
    }
    return { name: "windows", arch, compiler: "msvc2022", extension: "zip" };
  }
  throw new Error(`unsupported release platform: ${platform}`);
}

export function parseArguments(arguments_) {
  const options = { check: false, allowDirty: false, expectedVersion: undefined };
  for (let index = 0; index < arguments_.length; ++index) {
    const argument = arguments_[index];
    if (argument === "--check") {
      options.check = true;
    } else if (argument === "--allow-dirty") {
      options.allowDirty = true;
    } else if (argument === "--expected-version") {
      options.expectedVersion = arguments_[++index];
      if (!options.expectedVersion) {
        throw new Error("--expected-version requires a value");
      }
    } else {
      throw new Error(`unknown argument: ${argument}`);
    }
  }
  return options;
}

export function parseFingerprint(source) {
  return Object.fromEntries(
    source.trim().split(/\r?\n/).map((line) => line.split("=").map((value) => value.trim())),
  );
}

function run(command, arguments_, options = {}) {
  console.log(`\n> ${command} ${arguments_.join(" ")}`);
  const result = spawnSync(command, arguments_, {
    cwd: options.cwd ?? projectRoot,
    encoding: options.capture ? "utf8" : undefined,
    env: process.env,
    stdio: options.capture ? ["ignore", "pipe", "inherit"] : "inherit",
  });
  if (result.error) throw result.error;
  if (result.status !== 0) process.exit(result.status ?? 1);
  return options.capture ? result.stdout : "";
}

async function filesBelow(root, relative = "") {
  const entries = await readdir(path.join(root, relative), { withFileTypes: true });
  const files = [];
  for (const entry of entries) {
    const child = path.join(relative, entry.name);
    if (entry.isDirectory()) files.push(...(await filesBelow(root, child)));
    else if (entry.isFile()) files.push(child.split(path.sep).join("/"));
  }
  return files.sort();
}

function requirePath(files, description, predicate) {
  if (!files.some(predicate)) {
    throw new Error(`release staging tree is missing ${description}`);
  }
}

export function auditReleaseFiles(kind, files, windows = process.platform === "win32") {
  const forbidden = /(^|\/)(\.codex|raw|build|node_modules|secrets?)(\/|\.|$)|CCBridge\.rar$/i;
  const leaked = files.find((file) => forbidden.test(file));
  if (leaked) throw new Error(`forbidden release path: ${leaked}`);

  requirePath(files, "LICENSE", (file) => file === "share/doc/OXQF/LICENSE");
  requirePath(files, "README", (file) => file === "share/doc/OXQF/README.md");
  requirePath(files, "CLI contract", (file) => file === "share/doc/OXQF/cli.md");

  if (kind === "cli") {
    const executable = windows ? "bin/oxq.exe" : "bin/oxq";
    requirePath(files, executable, (file) => file === executable);
    if (files.some((file) => file.startsWith("include/") || file.startsWith("lib/"))) {
      throw new Error("CLI archive unexpectedly contains SDK files");
    }
    return;
  }

  requirePath(files, "core headers", (file) => file === "include/oxq/core/writer.hpp");
  requirePath(files, "convert headers", (file) => file === "include/oxq/convert/cbl_writer.hpp");
  requirePath(files, "CMake package", (file) => file === "lib/cmake/OXQF/OXQFConfig.cmake");
  requirePath(files, "oxq-core library", (file) => /(^|\/)lib\/?(lib)?oxq-core\.(a|lib)$/i.test(file));
  requirePath(files, "oxq-convert library", (file) => /(^|\/)lib\/?(lib)?oxq-convert\.(a|lib)$/i.test(file));
  requirePath(files, "OXQ vectors", (file) => file === "share/OXQF/vectors/oxq-v1/manifest.json");
  requirePath(files, "CBL vectors", (file) => file === "share/OXQF/vectors/cbl-v3/SHA256SUMS");
  if (files.some((file) => file === "bin/oxq" || file === "bin/oxq.exe")) {
    throw new Error("SDK archive unexpectedly contains the CLI executable");
  }
}

async function sha256(file) {
  return createHash("sha256").update(await readFile(file)).digest("hex");
}

async function archive(stagingDirectory, rootName, outputFile, extension) {
  const arguments_ = ["-E", "tar", extension === "zip" ? "cf" : "czf", outputFile];
  if (extension === "zip") arguments_.push("--format=zip");
  arguments_.push("--", rootName);
  run("cmake", arguments_, { cwd: stagingDirectory });
}

async function installComponent(buildDirectory, prefix, component) {
  run("cmake", [
    "--install",
    buildDirectory,
    "--prefix",
    prefix,
    "--config",
    "Release",
    "--component",
    component,
  ]);
}

async function main() {
  const options = parseArguments(process.argv.slice(2));
  const cmakeSource = await readFile(path.join(projectRoot, "CMakeLists.txt"), "utf8");
  const npmPackage = JSON.parse(await readFile(path.join(projectRoot, "package.json"), "utf8"));
  const version = projectVersion(cmakeSource);
  if (npmPackage.version !== version) {
    throw new Error(`package.json ${npmPackage.version} does not match CMake ${version}`);
  }
  const expectedVersion = options.expectedVersion ?? process.env.OXQF_RELEASE_VERSION;
  if (expectedVersion && expectedVersion !== version) {
    throw new Error(`requested version ${expectedVersion} does not match source ${version}`);
  }
  if (!options.allowDirty) {
    const status = run("git", ["status", "--porcelain"], { capture: true });
    if (status.trim()) {
      throw new Error("release packaging requires a clean Git working tree");
    }
  }

  const platform = releasePlatform();
  const buildDirectory = path.join(projectRoot, "build", "release-package");
  const stagingDirectory = path.join(buildDirectory, "staging");
  const outputDirectory = path.join(projectRoot, "out", "release");
  await rm(buildDirectory, { recursive: true, force: true });
  await mkdir(buildDirectory, { recursive: true });

  const configure = ["-S", projectRoot, "-B", buildDirectory];
  if (platform.name === "windows") {
    configure.push("-G", "Visual Studio 17 2022", "-A", "x64");
  } else {
    configure.push("-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release");
  }
  run("cmake", configure);
  run("cmake", ["--build", buildDirectory, "--config", "Release"]);
  run("ctest", ["--test-dir", buildDirectory, "-C", "Release", "--output-on-failure"]);
  run("cmake", [
    "--build",
    buildDirectory,
    "--config",
    "Release",
    "--target",
    "oxq_writer_fingerprint",
  ]);

  const cliName = `oxq-${version}-${platform.name}-${platform.arch}`;
  const sdkName = `oxq-sdk-${version}-${platform.name}-${platform.compiler}-${platform.arch}`;
  const cliRoot = path.join(stagingDirectory, cliName);
  const sdkRoot = path.join(stagingDirectory, sdkName);
  await installComponent(buildDirectory, cliRoot, "Runtime");
  await installComponent(buildDirectory, cliRoot, "Documentation");
  await installComponent(buildDirectory, sdkRoot, "Development");
  await installComponent(buildDirectory, sdkRoot, "Documentation");
  await installComponent(buildDirectory, sdkRoot, "Vectors");

  auditReleaseFiles("cli", await filesBelow(cliRoot), platform.name === "windows");
  auditReleaseFiles("sdk", await filesBelow(sdkRoot), platform.name === "windows");
  if (options.check) {
    console.log(`\nRelease staging check passed for OXQF ${version} (${platform.name}/${platform.arch})`);
    return;
  }

  await rm(outputDirectory, { recursive: true, force: true });
  await mkdir(outputDirectory, { recursive: true });
  const cliArchive = path.join(outputDirectory, `${cliName}.${platform.extension}`);
  const sdkArchive = path.join(outputDirectory, `${sdkName}.${platform.extension}`);
  await archive(stagingDirectory, cliName, cliArchive, platform.extension);
  await archive(stagingDirectory, sdkName, sdkArchive, platform.extension);

  const fingerprint = await readFile(
    path.join(buildDirectory, "writer-fingerprint.txt"),
    "utf8",
  );
  const artifacts = [];
  for (const file of [cliArchive, sdkArchive]) {
    artifacts.push({
      name: path.basename(file),
      bytes: (await stat(file)).size,
      sha256: await sha256(file),
    });
  }
  const manifestName = `release-manifest-${platform.name}-${platform.arch}.json`;
  const manifestPath = path.join(outputDirectory, manifestName);
  await writeFile(
    manifestPath,
    `${JSON.stringify({
      schema: "oxqf-release-manifest-v1",
      version,
      platform: platform.name,
      architecture: platform.arch,
      compiler: platform.compiler,
      writer_fingerprint: parseFingerprint(fingerprint),
      artifacts,
    }, null, 2)}\n`,
  );

  const checksumFiles = [...artifacts, {
    name: manifestName,
    sha256: await sha256(manifestPath),
  }].sort((left, right) => left.name.localeCompare(right.name));
  await writeFile(
    path.join(outputDirectory, `SHA256SUMS-${platform.name}-${platform.arch}`),
    `${checksumFiles.map((entry) => `${entry.sha256}  ${entry.name}`).join("\n")}\n`,
  );
  console.log(`\nRelease packages written to ${outputDirectory}`);
}

if (fileURLToPath(import.meta.url) === process.argv[1]) {
  main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
