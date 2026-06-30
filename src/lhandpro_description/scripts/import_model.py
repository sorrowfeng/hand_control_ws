#!/usr/bin/env python3

import argparse
import re
import shutil
from pathlib import Path


DESCRIPTION_PACKAGE = 'lhandpro_description'
TEXT_SUFFIXES = {
    '.csv',
    '.launch',
    '.py',
    '.txt',
    '.urdf',
    '.xacro',
    '.xml',
    '.yaml',
    '.yml',
}

PACKAGE_URI_RE = re.compile(r'package://([^/"\'<>,\s]+)/([^"\'<>,\s]+)')
RELATIVE_RESOURCE_RE = re.compile(
    r'(?P<prefix>(?:filename|file)=["\'])(?P<path>(?:\.\./)?(?:meshes|textures|urdf|mjcf)/[^"\']+)(?P<suffix>["\'])'
)


def read_text(path):
    for encoding in ('utf-8', 'gb18030', 'latin-1'):
        try:
            return path.read_text(encoding=encoding), encoding
        except UnicodeDecodeError:
            continue
    return path.read_text(), 'utf-8'


def rewrite_package_uri(text, model_id):
    def replace(match):
        resource_path = match.group(2).replace('\\', '/')
        if match.group(1) == DESCRIPTION_PACKAGE and resource_path.startswith('models/'):
            return match.group(0)
        return f'package://{DESCRIPTION_PACKAGE}/models/{model_id}/{resource_path}'

    return PACKAGE_URI_RE.sub(replace, text)


def rewrite_relative_resource(text, model_id):
    def replace(match):
        resource_path = match.group('path').replace('\\', '/')
        if resource_path.startswith('../'):
            resource_path = resource_path[3:]
        return (
            f"{match.group('prefix')}"
            f"package://{DESCRIPTION_PACKAGE}/models/{model_id}/{resource_path}"
            f"{match.group('suffix')}"
        )

    return RELATIVE_RESOURCE_RE.sub(replace, text)


def normalize_text_files(model_path, model_id):
    updated_files = []
    for path in model_path.rglob('*'):
        if not path.is_file() or path.suffix.lower() not in TEXT_SUFFIXES:
            continue

        original, _ = read_text(path)
        updated = rewrite_relative_resource(rewrite_package_uri(original, model_id), model_id)
        if updated != original:
            path.write_text(updated, encoding='utf-8', newline='')
            updated_files.append(path)

    return updated_files


def ensure_child_path(parent, child):
    parent = parent.resolve()
    child = child.resolve()
    if parent != child and parent not in child.parents:
        raise RuntimeError(f'Target path is outside models directory: {child}')


def import_model(source, models_dir, model_id, force):
    source = source.resolve()
    models_dir = models_dir.resolve()
    target = models_dir / model_id

    if not source.exists() or not source.is_dir():
        raise RuntimeError(f'Source model directory does not exist: {source}')

    ensure_child_path(models_dir, target)
    models_dir.mkdir(parents=True, exist_ok=True)

    if target.exists():
        if not force:
            raise RuntimeError(f'Target already exists, use --force to replace: {target}')
        shutil.rmtree(target)

    shutil.copytree(source, target)
    updated_files = normalize_text_files(target, model_id)
    return target, updated_files


def parse_args():
    parser = argparse.ArgumentParser(description='Import a vendor hand model into lhandpro_description/models.')
    parser.add_argument('source', type=Path, help='Vendor model directory to import')
    parser.add_argument('--model-id', help='Model directory name, defaults to source folder name')
    parser.add_argument('--models-dir', type=Path, default=Path(__file__).resolve().parents[1] / 'models')
    parser.add_argument('--force', action='store_true', help='Replace the target model directory if it exists')
    return parser.parse_args()


def main():
    args = parse_args()
    model_id = args.model_id or args.source.name
    target, updated_files = import_model(args.source, args.models_dir, model_id, args.force)

    print(f'Imported {args.source} -> {target}')
    print(f'Normalized {len(updated_files)} text files')
    for path in updated_files:
        print(f'  {path}')


if __name__ == '__main__':
    main()
