import { fileURLToPath } from "node:url";

export const requiredNodeMajor = 24;

export function checkNodeVersion(version = process.versions.node) {
  const actualMajor = Number.parseInt(version.split(".")[0], 10);
  return {
    ok: actualMajor === requiredNodeMajor,
    message:
      actualMajor === requiredNodeMajor
        ? `Node.js ${version} satisfies the development baseline`
        : `Node.js ${requiredNodeMajor}.x is required; current version is ${version}`,
  };
}

if (fileURLToPath(import.meta.url) === process.argv[1]) {
  const result = checkNodeVersion();
  const output = result.ok ? console.log : console.error;
  output(result.message);
  if (!result.ok) {
    process.exitCode = 1;
  }
}
