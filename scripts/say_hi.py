import os
from github import Github, Auth

GITHUB_TOKEN = os.getenv("GITHUB_TOKEN")
if not GITHUB_TOKEN:
    raise ValueError("请设置 GITHUB_TOKEN 环境变量")

REPO_NAME = os.getenv("GITHUB_REPOSITORY")  # 建议从环境变量读取
if not REPO_NAME:
    raise ValueError("请设置 GITHUB_REPOSITORY 环境变量")

PR_NUMBER = int(os.getenv("PR_NUMBER", 0))
if not PR_NUMBER:
    raise ValueError("请设置 PR_NUMBER 环境变量")

def main():
    g = Github(auth=Auth.Token(GITHUB_TOKEN))
    repo = g.get_repo(REPO_NAME)
    pr = repo.get_pull(PR_NUMBER)
    pr.create_issue_comment(f"Hi, PR #{PR_NUMBER}")  # 修正方法名

if __name__ == '__main__':
    main()