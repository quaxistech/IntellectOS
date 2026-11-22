"""Host-side first system identity and timezone policy.
No plaintext password is ever written; not linked into kernel yet.
"""
from __future__ import annotations
import base64
import hmac
import json
import os
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

class ConfigError(ValueError):
    pass


def _argon2_backend():
    """Only provisioning/login checks depend on optional host Argon2id.

    Importing timezone validators, IFS readers or image builders must not
    require the password provisioning dependency. Do not silently weaken the
    KDF or accept credentials when Argon2id is missing.
    """
    try:
        from argon2.low_level import hash_secret_raw, Type
    except ModuleNotFoundError as exc:
        if exc.name not in ('argon2', 'argon2.low_level'):
            raise
        raise RuntimeError(
            'Argon2id backend is missing: install python3-argon2 '
            '(Ubuntu 24.04) or argon2-cffi in a virtual environment'
        ) from exc
    return hash_secret_raw, Type


def validate_timezone(name: str) -> str:
    if (not isinstance(name,str) or len(name)>96 or not name or name.startswith('/')
        or name.endswith('/') or '..' in name or '\\' in name
        or any(not (c.isalnum() and c.isascii() or c in '_-/+') for c in name)):
        raise ConfigError('invalid IANA timezone name')
    try:
        ZoneInfo(name)
    except (ZoneInfoNotFoundError, ValueError) as exc:
        raise ConfigError('timezone unknown to local IANA database') from exc
    return name


def _parse_secret(password: str) -> bytes:
    if not isinstance(password, str) or '\0' in password:
        raise ConfigError('password must be Unicode text with no NUL')
    encoded=password.encode('utf-8')
    if not 12<=len(encoded)<=256:
        raise ConfigError('password must be between 12 and 256 bytes UTF-8')
    return encoded


def provision(password: str, timezone: str, *, memory_kib: int=65536,
              iterations: int=3, parallelism: int=1) -> dict:
    validate_timezone(timezone)
    secret=_parse_secret(password)
    if not 65536<=memory_kib<=1048576 or not 2<=iterations<=12 or parallelism!=1:
        raise ConfigError('Argon2id production parameter bounds')
    hash_secret_raw, Type = _argon2_backend()
    salt=os.urandom(16)
    verifier=hash_secret_raw(secret,salt,iterations,memory_kib,parallelism,32,Type.ID,
                             version=19)
    return {'schema':'IntellectOS.system_config.v1',
            'timezone_iana':timezone,'rtc_policy':'UTC',
            'users':[{'name':'root','uid':0,'gid':0,
                      'password_kdf':{'kind':'Argon2id','version':19,
                                      'memory_kib':memory_kib,'iterations':iterations,
                                      'parallelism':parallelism,
                                      'salt':base64.b64encode(salt).decode('ascii'),
                                      'hash':base64.b64encode(verifier).decode('ascii')}}]}


def verify_password(record: dict, password: str) -> bool:
    try:
        if record.get('name')!='root' or record.get('uid')!=0:
            return False
        kdf=record['password_kdf']
        if kdf.get('kind')!='Argon2id' or kdf.get('version')!=19:
            return False
        m,t,p=kdf['memory_kib'],kdf['iterations'],kdf['parallelism']
        if not isinstance(m,int) or not isinstance(t,int) or not isinstance(p,int):
            return False
        if not 65536<=m<=1048576 or not 2<=t<=12 or p!=1:
            return False
        salt=base64.b64decode(kdf['salt'],validate=True)
        expect=base64.b64decode(kdf['hash'],validate=True)
        if len(salt)!=16 or len(expect)!=32:return False
        secret=_parse_secret(password)
        hash_secret_raw, Type = _argon2_backend()
        got=hash_secret_raw(secret,salt,t,m,p,32,Type.ID,version=19)
        return hmac.compare_digest(got,expect)
    except (KeyError,ValueError,TypeError,ConfigError):
        return False


def canonical_json(record: dict) -> bytes:
    if record.get('schema')!='IntellectOS.system_config.v1' or \
       len(record.get('users',[]))!=1 or record['users'][0].get('uid')!=0:
        raise ConfigError('invalid config record')
    validate_timezone(record['timezone_iana'])
    return (json.dumps(record,sort_keys=True,ensure_ascii=False,separators=(',',':'))+'\n').encode()
