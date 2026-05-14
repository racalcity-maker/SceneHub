import { z } from "zod";

export const gmVersionsSchema = z.object({
  generation: z.number().optional().default(0),
  rooms: z.number(),
  devices: z.number(),
  scenarios: z.number(),
  profiles: z.number(),
  ingest: z.number(),
  session: z.number(),
  static: z.number(),
  runtime: z.number(),
});

export type GmVersions = z.infer<typeof gmVersionsSchema>;

export function gmVersionsEqual(a: GmVersions | null, b: GmVersions | null): boolean {
  if (!a || !b) {
    return false;
  }
  return (
    a.rooms === b.rooms &&
    a.devices === b.devices &&
    a.scenarios === b.scenarios &&
    a.profiles === b.profiles &&
    a.ingest === b.ingest &&
    a.session === b.session &&
    a.static === b.static &&
    a.runtime === b.runtime
  );
}

export function gmVersionsChanged(versions: GmVersions | null, previous: GmVersions | null, keys: Array<keyof GmVersions>): boolean {
  if (!versions || !previous) {
    return false;
  }
  return keys.some((key) => versions[key] !== previous[key]);
}
