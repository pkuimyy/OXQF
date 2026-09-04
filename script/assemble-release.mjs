import { createHash } from "node:crypto";
import { readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import path from "node:path";
import process from "node:process";
import { fileURLToPath } from "node:url";

async function sha256(file) {
  return createHash("sha256").update(await readFile(file)).digest("hex");
}

export function equalFingerprints(left, right) {
  return left.sha256 === right.sha256 && left.framed_bytes === right.framed_bytes;
}

function validateManifest(manifest) {
  if (manifest.schema !== "oxqf-release-manifest-v2") {
    throw new Error("unsupported release manifest schema");
  }
  if (!/^\d+\.\d+\.\d+$/.test(manifest.version)) {
    throw new Error(`invalid release version: ${manifest.version}`);
  }
  const platforms = {
    linux: { compiler: "gcc", extension: "tar.gz" },
    windows: { compiler: "msvc2022", extension: "zip" },
  };
  const platform = platforms[manifest.platform];
  if (!platform || manifest.compiler !== platform.compiler || manifest.architecture !== "x86_64") {
    throw new Error(`unsupported release build: ${manifest.platform}/${manifest.compiler}/${manifest.architecture}`);
  }
  if (!/^[0-9a-f]{64}$/.test(manifest.writer_fingerprint?.sha256)
      || !/^\d+$/.test(manifest.writer_fingerprint?.framed_bytes)) {
    throw new Error("invalid Writer fingerprint in release manifest");
  }
  const expectedAsset = `oxq-${manifest.version}-${manifest.platform}-${manifest.compiler}-${manifest.architecture}.${platform.extension}`;
  if (!Array.isArray(manifest.artifacts) || manifest.artifacts.length !== 1) {
    throw new Error(`release manifest for ${manifest.platform} must contain one distribution archive`);
  }
  for (const artifact of manifest.artifacts) {
    if (artifact.name !== expectedAsset
        || artifact.name !== path.basename(artifact.name)
        || !Number.isSafeInteger(artifact.bytes)
        || artifact.bytes < 1
        || !/^[0-9a-f]{64}$/.test(artifact.sha256)) {
      throw new Error(`invalid release asset metadata: ${artifact.name}`);
    }
  }
}

export async function assembleRelease(directory, expectedVersion) {
  directory = path.resolve(directory);
  const names = await readdir(directory);
  const manifestNames = names
    .filter((name) => /^release-manifest-(linux|windows)-.+\.json$/.test(name))
    .sort();
  if (manifestNames.length !== 2) {
    throw new Error("exactly one Linux and one Windows release manifest are required");
  }
  const manifests = await Promise.all(
    manifestNames.map(async (name) => ({
      name,
      value: JSON.parse(await readFile(path.join(directory, name), "utf8")),
    })),
  );
  manifests.forEach(({ value }) => validateManifest(value));
  const platforms = manifests.map(({ value }) => value.platform).sort();
  if (platforms.join(",") !== "linux,windows") {
    throw new Error(`release manifests cover unexpected platforms: ${platforms.join(", ")}`);
  }
  const version = manifests[0].value.version;
  if (manifests.some(({ value }) => value.version !== version)) {
    throw new Error("release manifest versions differ");
  }
  if (expectedVersion && version !== expectedVersion) {
    throw new Error(`release manifest ${version} does not match requested ${expectedVersion}`);
  }
  if (!equalFingerprints(
    manifests[0].value.writer_fingerprint,
    manifests[1].value.writer_fingerprint,
  )) {
    throw new Error("Linux and Windows OXQ Writer fingerprints differ");
  }

  const artifacts = [];
  const seen = new Set();
  for (const { value } of manifests) {
    for (const artifact of value.artifacts) {
      if (seen.has(artifact.name)) throw new Error(`duplicate release asset: ${artifact.name}`);
      seen.add(artifact.name);
      const file = path.join(directory, artifact.name);
      const actualSize = (await stat(file)).size;
      const actualHash = await sha256(file);
      if (actualSize !== artifact.bytes || actualHash !== artifact.sha256) {
        throw new Error(`release asset does not match its manifest: ${artifact.name}`);
      }
      artifacts.push(artifact);
    }
  }
  artifacts.sort((left, right) => left.name.localeCompare(right.name));

  const combinedName = "combined-release-manifest.json";
  const combinedPath = path.join(directory, combinedName);
  await writeFile(
    combinedPath,
    `${JSON.stringify({
      schema: "oxqf-release-manifest-v2",
      version,
      writer_fingerprint: manifests[0].value.writer_fingerprint,
      builds: manifests.map(({ value }) => ({
        platform: value.platform,
        architecture: value.architecture,
        compiler: value.compiler,
      })),
      artifacts,
    }, null, 2)}\n`,
  );

  for (const name of names.filter((name) => name.startsWith("SHA256SUMS"))) {
    await rm(path.join(directory, name), { force: true });
  }
  const checksumNames = artifacts.map(({ name }) => name).sort();
  const checksumLines = [];
  for (const name of checksumNames) {
    checksumLines.push(`${await sha256(path.join(directory, name))}  ${name}`);
  }
  await writeFile(path.join(directory, "SHA256SUMS"), `${checksumLines.join("\n")}\n`);
  console.log(
    `Assembled OXQF ${version}: ${artifacts.length} binary archives; Writer ${manifests[0].value.writer_fingerprint.sha256}`,
  );
}

async function main() {
  await assembleRelease(process.argv[2] ?? "out/release", process.argv[3]);
}

if (fileURLToPath(import.meta.url) === process.argv[1]) {
  main().catch((error) => {
    console.error(error.message);
    process.exitCode = 1;
  });
}
